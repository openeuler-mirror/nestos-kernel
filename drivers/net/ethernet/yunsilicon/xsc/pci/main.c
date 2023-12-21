// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 - 2023, Shanghai Yunsilicon Technology Co., Ltd.
 * All rights reserved.
 */

#include <common/xsc_core.h>
#include <common/driver.h>
#include <common/cq.h>
#include <common/qp.h>
#include <common/xsc_lag.h>
#include <common/xsc_port_ctrl.h>
#ifdef CONFIG_XSC_ESWITCH
#include "devlink.h"
#include "eswitch.h"
#endif
#include "fw/xsc_counters.h"
#include "fw/xsc_tbm.h"
#include "xsc_pci_ctrl.h"

#ifdef RUN_WITH_PSV
#include "../../../../xscale-fw/include/xscale-fw.h"
#endif

unsigned int xsc_debug_mask;
module_param_named(debug_mask, xsc_debug_mask, uint, 0644);
MODULE_PARM_DESC(debug_mask,
	"debug mask: 1=dump cmd data, 2=dump cmd exec time, 3=both. Default=0");

bool hw_init = 1;
int pcie_no;
module_param_named(hw_init, hw_init, bool, 0644);
module_param_named(pcie_no, pcie_no, int, 0644);

static bool probe_vf = 1;
module_param_named(probe_vf, probe_vf, bool, 0644);
MODULE_PARM_DESC(probe_vf, "probe VFs or not, 0 = not probe, 1 = probe. Default = 1");

static bool xsc_hw_reset;

#define DRIVER_NAME			"xsc_pci"
#define DRIVER_VERSION			"0.1.0"

#define XSC_PCI_VENDOR_ID_OBSOLETE	0x1172
#define XSC_PCI_VENDOR_ID		0x1f67
#define XSC_PF1_DEVICE_ID		0x0001
#define XSC_PF1_VF_DEVICE_ID		0x0002

#ifdef USE_VIRTIO
static const struct pci_device_id xsc_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REDHAT_QUMRANET, PCI_ANY_ID) },
	{ 0 }
};
#else
static const struct pci_device_id xsc_pci_id_table[] = {
	{ PCI_DEVICE(XSC_PCI_VENDOR_ID, XSC_PF1_DEVICE_ID) },
	{ PCI_DEVICE(XSC_PCI_VENDOR_ID_OBSOLETE, XSC_PF1_DEVICE_ID) },
	{ PCI_DEVICE(XSC_PCI_VENDOR_ID, XSC_PF1_VF_DEVICE_ID),
		.driver_data = XSC_PCI_DEV_IS_VF}, /* PF1's VF */
	{ 0 }
};
#endif /* !USE_VIRTIO */
MODULE_DEVICE_TABLE(pci, xsc_pci_id_table);

#ifndef RUN_WITH_PSV
static bool need_write_reg_directly(void *in)
{
	struct xsc_inbox_hdr *hdr;
	struct xsc_ioctl_mbox_in *req;
	struct xsc_ioctl_data_tl *tl;
	char *data;

	hdr = (struct xsc_inbox_hdr *)in;
	if (unlikely(be16_to_cpu(hdr->opcode) == XSC_CMD_OP_IOCTL_FLOW)) {
		req = (struct xsc_ioctl_mbox_in *)in;
		data = (char *)req->data;
		tl = (struct xsc_ioctl_data_tl *)data;
		if (tl->opmod == XSC_IOCTL_OP_ADD) {
			if (unlikely(tl->table == XSC_FLOW_DMA_WR || tl->table == XSC_FLOW_DMA_RD))
				return true;
		}
	}
	return false;
}

#else
static u8 phyport_num;

static u32 xsc_get_glb_func_id(struct xsc_core_device *dev)
{
	u16 vf_id, vf_bdf;
	struct pci_dev *pdev = dev->pdev;
	struct xsc_core_device *pf_xdev;

	if (xsc_core_is_pf(dev)) {
		if (xsc_get_pcie_no() == 0)
			return XSC_PCIE0_PF_N_FUNC_ID(phyport_num++);
		else
			return XSC_PCIE1_PF_N_FUNC_ID(phyport_num++);
	} else {
		pf_xdev = pci_get_drvdata(pdev->physfn);
		vf_bdf = (pdev->bus->number << 8) | pdev->devfn;
		if (xsc_get_pcie_no() == 0 && vf_bdf >= pf_xdev->priv.sriov.vf_bdf_base) {
			vf_id = vf_bdf - pf_xdev->priv.sriov.vf_bdf_base;
			if (pf_xdev->pf_id == 0)
				return XSC_PCIE0_PF0_VF_N_FUNC_ID(vf_id);
			else
				return XSC_PCIE0_PF1_VF_N_FUNC_ID(vf_id);
		}
	}

	return 0;
}
#endif

int xsc_cmd_exec(struct xsc_core_device *dev, void *in, int in_size, void *out,
		 int out_size)
{
#ifndef RUN_WITH_PSV
	int ret;

	if (need_write_reg_directly(in))
		ret = xsc_cmd_write_reg_directly(dev, in, in_size, out, out_size, dev->glb_func_id);
	else
		ret = _xsc_cmd_exec(dev, in, in_size, out, out_size);

	return ret;
#else
	return xsc_cmd_exec_psv(dev, in, in_size, out, out_size, dev->glb_func_id);
#endif
}
EXPORT_SYMBOL(xsc_cmd_exec);

int xsc_get_pcie_no(void)
{
	return pcie_no;
}
EXPORT_SYMBOL(xsc_get_pcie_no);

static int set_dma_caps(struct pci_dev *pdev)
{
	int err = 0;

	err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	} else {
		err = dma_set_coherent_mask(&pdev->dev,
#ifdef USE_VIRTIO
			DMA_BIT_MASK(32 + VIRTIO_PCI_QUEUE_ADDR_SHIFT));
#else
			DMA_BIT_MASK(64));
#endif /* !USE_VIRTIO */
	}

	if (!err)
		dma_set_max_seg_size(&pdev->dev, 2u * 1024 * 1024 * 1024);

	return err;
}

static void xsc_pci_get_bdf(struct xsc_core_device *dev)
{
	struct pci_dev *pci_dev = dev->pdev;

	dev->bus_id = pci_dev->bus->number;
	dev->dev_id = PCI_SLOT(pci_dev->devfn);
	dev->func_id = PCI_FUNC(pci_dev->devfn);

	xsc_core_info(dev, "%s: bdf=%04x.%04x.%04x\n",
		__func__, dev->bus_id, dev->dev_id, dev->func_id);
}

static void xsc_pci_calc_pf_port(struct xsc_core_device *dev)
{
	dev->pf = xsc_cal_pf_vf_id(dev->glb_func_id, &dev->pf_id,
				&dev->pcie, &dev->vf_id) ? 1 : 0;
	if (dev->pcie == 0) {
		dev->pcie_port = XSC_PHY_PORT_PCIE_N(0);
		dev->logic_port = XSC_PCIE0_PF_N_LOGIC_PORT(dev->pf_id);

	} else {
		dev->pcie_port = XSC_PHY_PORT_PCIE_N(1);
		dev->logic_port = XSC_PCIE1_PF_N_LOGIC_PORT(dev->pf_id);
	}
	dev->pf_logic_port = dev->logic_port;
	dev->mac_logic_port = dev->mac_port = dev->caps.mac_port;

	xsc_core_dbg(dev,
		"glb_func=%d, pcie_port=%d, pf_logic_port=%d, mac_port=%d, board_id=%d\n",
		dev->glb_func_id, dev->pcie_port, dev->logic_port,
		dev->mac_port, dev->board_id);
}

void xsc_pci_get_vf_info(struct xsc_core_device *dev, struct xsc_vf_info *info)
{
	if (info->phy_port == 0) {
		if (info->pf_id == 0) {
			info->logic_port = XSC_PCIE0_PF0_VF_N_LOGIC_PORT(info->vf_id);
			info->func_id = XSC_PCIE0_PF0_VF_N_FUNC_ID(info->vf_id);
		} else {
			info->logic_port = XSC_PCIE0_PF1_VF_N_LOGIC_PORT(info->vf_id);
			info->func_id = XSC_PCIE0_PF1_VF_N_FUNC_ID(info->vf_id);
		}
	} else {
		if (info->pf_id == 0) {
			info->logic_port = XSC_PCIE1_PF0_VF_N_LOGIC_PORT(info->vf_id);
			info->func_id = XSC_PCIE1_PF0_VF_N_FUNC_ID(info->vf_id);
		} else {
			info->logic_port = XSC_PCIE1_PF1_VF_N_LOGIC_PORT(info->vf_id);
			info->func_id = XSC_PCIE1_PF1_VF_N_FUNC_ID(info->vf_id);
		}
	}
}
EXPORT_SYMBOL(xsc_pci_get_vf_info);

static void xsc_pci_calc_vf_port(struct xsc_core_device *dev)
{
	dev->pf = xsc_cal_pf_vf_id(dev->glb_func_id, &dev->pf_id, &dev->pcie, &dev->vf_id);
	if (unlikely(dev->pcie == 1))
		return;

	if (dev->pf_id == 0)
		dev->logic_port = XSC_PCIE0_PF0_VF_N_LOGIC_PORT(dev->vf_id);
	else
		dev->logic_port = XSC_PCIE0_PF1_VF_N_LOGIC_PORT(dev->vf_id);

	dev->pcie_port = XSC_PHY_PORT_PCIE_N(0);
	dev->pf_logic_port = XSC_PCIE0_PF_N_LOGIC_PORT(dev->pf_id);
	dev->mac_logic_port = dev->mac_port = dev->caps.mac_port;

	xsc_core_dbg(dev,
		"vf%d_logic_port=%d, glb_func_id=%d, pf%d_logic_port=%d, mac_logic_port=%d, board_id=%d\n",
		dev->vf_id, dev->logic_port, dev->glb_func_id, dev->pf_id,
		dev->pf_logic_port, dev->mac_logic_port, dev->board_id);
}

static int xsc_pci_enable_device(struct xsc_core_device *dev)
{
	struct pci_dev *pdev = dev->pdev;
	int err = 0;

	mutex_lock(&dev->pci_status_mutex);
	if (dev->pci_status == XSC_PCI_STATUS_DISABLED) {
		err = pci_enable_device(pdev);
		if (!err)
			dev->pci_status = XSC_PCI_STATUS_ENABLED;
	}
	mutex_unlock(&dev->pci_status_mutex);

	return err;
}

static void xsc_pci_disable_device(struct xsc_core_device *dev)
{
	struct pci_dev *pdev = dev->pdev;

	mutex_lock(&dev->pci_status_mutex);
	if (dev->pci_status == XSC_PCI_STATUS_ENABLED) {
		pci_disable_device(pdev);
		dev->pci_status = XSC_PCI_STATUS_DISABLED;
	}
	mutex_unlock(&dev->pci_status_mutex);
}

int xsc_priv_init(struct xsc_core_device *dev)
{
	struct xsc_priv *priv = &dev->priv;

	strncpy(priv->name, dev_name(&dev->pdev->dev), XSC_MAX_NAME_LEN);
	priv->name[XSC_MAX_NAME_LEN - 1] = 0;

	INIT_LIST_HEAD(&priv->ctx_list);
	spin_lock_init(&priv->ctx_lock);
	mutex_init(&dev->intf_state_mutex);

	return 0;
}

int xsc_dev_res_init(struct xsc_core_device *dev)
{
	struct xsc_dev_resource *dev_res = NULL;

	dev_res = kvzalloc(sizeof(*dev_res), GFP_KERNEL);
	if (!dev_res)
		return -ENOMEM;

	dev->dev_res = dev_res;
	/* init access lock */
	spin_lock_init(&dev->reg_access_lock.lock);
	mutex_init(&dev_res->alloc_mutex);
	mutex_init(&dev_res->pgdir_mutex);
	INIT_LIST_HEAD(&dev_res->pgdir_list);
	spin_lock_init(&dev_res->mkey_lock);

	return 0;
}

void xsc_dev_res_cleanup(struct xsc_core_device *dev)
{
	kfree(dev->dev_res);
	dev->dev_res = NULL;
}

void xsc_init_reg_addr(struct xsc_core_device *dev)
{
	if (xsc_core_is_pf(dev)) {
		dev->regs.cpm_get_lock = HIF_CPM_LOCK_GET_REG_ADDR;
		dev->regs.cpm_put_lock = HIF_CPM_LOCK_PUT_REG_ADDR;
		dev->regs.cpm_lock_avail = HIF_CPM_LOCK_AVAIL_REG_ADDR;
		dev->regs.cpm_data_mem = HIF_CPM_IDA_DATA_MEM_ADDR;
		dev->regs.cpm_cmd = HIF_CPM_IDA_CMD_REG_ADDR;
		dev->regs.cpm_addr = HIF_CPM_IDA_ADDR_REG_ADDR;
		dev->regs.cpm_busy = HIF_CPM_IDA_BUSY_REG_ADDR;
	} else {
		dev->regs.tx_db = TX_DB_FUNC_MEM_ADDR;
		dev->regs.rx_db = RX_DB_FUNC_MEM_ADDR;
		dev->regs.complete_db = DB_CQ_FUNC_MEM_ADDR;
		dev->regs.complete_reg = DB_CQ_CID_DIRECT_MEM_ADDR;
		dev->regs.event_db = DB_EQ_FUNC_MEM_ADDR;
		dev->regs.cpm_get_lock = CPM_LOCK_GET_REG_ADDR;
		dev->regs.cpm_put_lock = CPM_LOCK_PUT_REG_ADDR;
		dev->regs.cpm_lock_avail = CPM_LOCK_AVAIL_REG_ADDR;
		dev->regs.cpm_data_mem = CPM_IDA_DATA_MEM_ADDR;
		dev->regs.cpm_cmd = CPM_IDA_CMD_REG_ADDR;
		dev->regs.cpm_addr = CPM_IDA_ADDR_REG_ADDR;
		dev->regs.cpm_busy = CPM_IDA_BUSY_REG_ADDR;
	}
}

int xsc_dev_init(struct xsc_core_device *dev)
{
	int err = 0;

	xsc_priv_init(dev);

	err = xsc_dev_res_init(dev);
	if (err) {
		xsc_core_err(dev, "xsc dev res init failed %d\n", err);
		goto err_res_init;
	}

	/* create debugfs */
	err = xsc_debugfs_init(dev);
	if (err) {
		xsc_core_err(dev, "xsc_debugfs_init failed %d\n", err);
		goto err_debugfs_init;
	}

	err = xsc_pagealloc_init(dev);
	if (err) {
		xsc_core_err(dev, "xsc_pagealloc_init failed %d\n", err);
		goto err_pagealloc_init;
	}

	xsc_init_reg_addr(dev);

	return 0;

err_pagealloc_init:
	xsc_debugfs_fini(dev);
err_debugfs_init:
	xsc_dev_res_cleanup(dev);
err_res_init:
	return err;
}

void xsc_dev_cleanup(struct xsc_core_device *dev)
{
//	xsc_pagealloc_stop(dev);
//	xsc_reclaim_startup_pages(dev);
	xsc_pagealloc_cleanup(dev);
//	iounmap(dev->iseg);
	xsc_debugfs_fini(dev);
	xsc_dev_res_cleanup(dev);
}

static int xsc_pci_init(struct xsc_core_device *dev, const struct pci_device_id *id)
{
	struct pci_dev *pdev = dev->pdev;
	int err = 0;
	int bar_num = 0;
	void __iomem *bar_base = NULL;
	char name[16];

#ifdef USE_VIRTIO
	snprintf(name, sizeof(name), "%s", "xsc-virtio");
#else
	snprintf(name, sizeof(name), "%s", "xsc-pci");
	if (id->vendor == XSC_PCI_VENDOR_ID_OBSOLETE)
		bar_num = 2;
#endif

	mutex_init(&dev->pci_status_mutex);
	dev->priv.numa_node = dev_to_node(&pdev->dev);
	if (dev->priv.numa_node == -1)
		dev->priv.numa_node = 0;

	/* enable the device */
	err = xsc_pci_enable_device(dev);
	if (err) {
		xsc_core_err(dev, "failed to enable PCI device: err=%d\n", err);
		goto err_ret;
	}

	err = pci_request_region(pdev, bar_num, name);
	if (err) {
		xsc_core_err(dev, "failed to request %s pci_region=%d: err=%d\n",
			name, bar_num, err);
		goto err_disable;
	}

	pci_set_master(pdev);

	err = set_dma_caps(pdev);
	if (err) {
		xsc_core_err(dev, "failed to set DMA capabilities mask: err=%d\n", err);
		goto err_clr_master;
	}

	bar_base = pci_ioremap_bar(pdev, bar_num);
	if (!bar_base) {
		xsc_core_err(dev, "failed to ioremap %s bar%d\n", name, bar_num);
		goto err_clr_master;
	} else {
		xsc_core_info(dev, "ioremap bar%d base address=0x%llx\n", bar_num,
			(unsigned long long)bar_base);
	}

	err = pci_save_state(pdev);
	if (err) {
		xsc_core_err(dev, "pci_save_state failed: err=%d\n", err);
		goto err_io_unmap;
	}

	dev->bar_num = bar_num;
#ifdef USE_VIRTIO
	dev->bar0 = bar_base;
#else
	dev->bar2 = bar_base;
#endif /* !USE_VIRTIO */
	xsc_pci_get_bdf(dev);

	xsc_init_reg_addr(dev);

	return 0;

err_io_unmap:
	pci_iounmap(pdev, bar_base);
err_clr_master:
	pci_clear_master(pdev);
	pci_release_region(pdev, bar_num);
err_disable:
	xsc_pci_disable_device(dev);
err_ret:
	return err;
}

static void xsc_pci_fini(struct xsc_core_device *dev)
{
	struct pci_dev *pdev = dev->pdev;
	void __iomem *bar_base = NULL;

#ifdef USE_VIRTIO
	bar_base = dev->bar0;
#else
	bar_base = dev->bar2;
#endif

	if (bar_base)
		pci_iounmap(pdev, bar_base);
	pci_clear_master(pdev);
	pci_release_region(pdev, dev->bar_num);
	xsc_pci_disable_device(dev);
}

static int xsc_check_cmdq_version(struct xsc_core_device *dev)
{
	struct xsc_cmd_query_cmdq_ver_mbox_out *out;
	struct xsc_cmd_query_cmdq_ver_mbox_in in;

	int err;

	out = kzalloc(sizeof(*out), GFP_KERNEL);
	if (!out) {
		err = -ENOMEM;
		goto no_mem_out;
	}

	memset(&in, 0, sizeof(in));
	in.hdr.opcode = cpu_to_be16(XSC_CMD_OP_QUERY_CMDQ_VERSION);
	in.hdr.opmod = cpu_to_be16(0x1);
	err = xsc_cmd_exec(dev, &in, sizeof(in), out, sizeof(*out));
	if (err)
		goto out_out;

	if (out->hdr.status) {
		err = xsc_cmd_status_to_err(&out->hdr);
		goto out_out;
	}

	if (be16_to_cpu(out->cmdq_ver) != CMDQ_VERSION) {
		xsc_core_err(dev, "cmdq version check failed, expecting version %d, actual version %d\n",
			CMDQ_VERSION, be16_to_cpu(out->cmdq_ver));
		err = -EINVAL;
		goto out_out;
	}
	dev->cmdq_ver = CMDQ_VERSION;

out_out:
	kfree(out);
no_mem_out:
	return err;
}

int xsc_reset_function_resource(struct xsc_core_device *dev)
{
	struct xsc_function_reset_mbox_in in;
	struct xsc_function_reset_mbox_out out;
	int err;

	memset(&in, 0, sizeof(in));
	in.hdr.opcode = cpu_to_be16(XSC_CMD_OP_FUNCTION_RESET);
	in.glb_func_id = cpu_to_be16(dev->glb_func_id);
	err = xsc_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err || out.hdr.status)
		return -EINVAL;

	return 0;
}

static int xsc_fpga_not_supported(struct xsc_core_device *dev)
{
#define FPGA_VERSION_H 0x100
	u32 ver_h;

	if (!xsc_core_is_pf(dev))
		return 0;

	ver_h = REG_RD32(dev, HIF_CPM_CHIP_VERSION_H_REG_ADDR);
	if (ver_h != FPGA_VERSION_H) {
		xsc_core_err(dev, "fpga version 0x%x not supported\n", ver_h);
		return 1;
	}

	return 0;
}

static int xsc_init_once(struct xsc_core_device *dev)
{
	int err;

	err = xsc_cmd_init(dev);
	if (err) {
		xsc_core_err(dev, "Failed initializing command interface, aborting\n");
		goto err_cmd_init;
	}

#ifdef RUN_WITH_PSV
	dev->glb_func_id = xsc_get_glb_func_id(dev);
#endif

	err = xsc_check_cmdq_version(dev);
	if (err) {
		xsc_core_err(dev, "Failed to check cmdq version\n");
		goto err_cmdq_ver_chk;
	}

	err = xsc_cmd_query_hca_cap(dev, &dev->caps);
	if (err) {
		xsc_core_err(dev, "Failed to query hca, err=%d\n", err);
		goto err_cmdq_ver_chk;
	}

	err = xsc_reset_function_resource(dev);
	if (err) {
		xsc_core_err(dev, "Failed to reset function resource\n");
		goto err_cmdq_ver_chk;
	}

	err = xsc_get_board_id(dev);
	if (err) {
		xsc_core_err(dev, "Failed to get board id, err=%d\n", err);
		goto err_cmdq_ver_chk;
	}
	if (xsc_core_is_pf(dev)) {
		xsc_pci_calc_pf_port(dev);
		err = xsc_create_res(dev);
		if (err) {
			xsc_core_err(dev, "Failed to create resource, err=%d\n", err);
			goto err_cmdq_ver_chk;
		}
	} else {
		xsc_pci_calc_vf_port(dev);
	}

	xsc_init_cq_table(dev);
	xsc_init_qp_table(dev);
	xsc_eq_init(dev);

#ifdef CONFIG_XSC_SRIOV
	err = xsc_sriov_init(dev);
	if (err) {
		xsc_core_err(dev, "Failed to init sriov %d\n", err);
		goto err_sriov_init;
	}
#ifdef CONFIG_XSC_ESWITCH
	err = xsc_eswitch_init(dev);
	if (err) {
		xsc_core_err(dev, "Failed to init eswitch %d\n", err);
		goto err_eswitch_init;
	}
#endif
#endif
	return 0;

#ifdef CONFIG_XSC_ESWITCH
err_eswitch_init:
	xsc_sriov_cleanup(dev);
#endif
#ifdef CONFIG_XSC_SRIOV
err_sriov_init:
#endif
	xsc_eq_cleanup(dev);
	xsc_cleanup_qp_table(dev);
	xsc_cleanup_cq_table(dev);
	if (xsc_core_is_pf(dev))
		xsc_destroy_res(dev);
err_cmdq_ver_chk:
	xsc_cmd_cleanup(dev);
err_cmd_init:
	return err;
}

static int xsc_cleanup_once(struct xsc_core_device *dev)
{
#ifdef CONFIG_XSC_SRIOV
#ifdef CONFIG_XSC_ESWITCH
	xsc_eswitch_cleanup(dev);
#endif
	xsc_sriov_cleanup(dev);
#endif
	xsc_eq_cleanup(dev);
	xsc_cleanup_qp_table(dev);
	xsc_cleanup_cq_table(dev);
	if (xsc_core_is_pf(dev))
		xsc_destroy_res(dev);
	xsc_cmd_cleanup(dev);
	return 0;
}

static int xsc_load(struct xsc_core_device *dev)
{
	int err;

	xsc_pagealloc_start(dev);

	err = xsc_irq_eq_create(dev);
	if (err) {
		xsc_core_err(dev, "xsc_irq_eq_create failed %d\n", err);
		goto err_irq_eq_create;
	}

#ifdef CONFIG_XSC_SRIOV
	err = xsc_sriov_attach(dev);
	if (err) {
		xsc_core_err(dev, "sriov init failed %d\n", err);
		goto err_sriov;
	}
#endif

	return 0;

#ifdef CONFIG_XSC_SRIOV
err_sriov:
	xsc_irq_eq_destroy(dev);
#endif
err_irq_eq_create:
	xsc_pagealloc_stop(dev);
	return err;
}

static int xsc_unload(struct xsc_core_device *dev)
{
	xsc_lag_remove_xdev(dev);
#ifdef CONFIG_XSC_SRIOV
	xsc_sriov_detach(dev);
#endif
	xsc_irq_eq_destroy(dev);
	xsc_pagealloc_stop(dev);

	return 0;
}

int xsc_load_one(struct xsc_core_device *dev, bool boot)
{
	int err = 0;

	mutex_lock(&dev->intf_state_mutex);
	if (test_bit(XSC_INTERFACE_STATE_UP, &dev->intf_state)) {
		xsc_core_warn(dev, "interface is up, NOP\n");
		goto out;
	}

	if (test_bit(XSC_INTERFACE_STATE_TEARDOWN, &dev->intf_state)) {
		xsc_core_warn(dev, "device is being removed, stop load\n");
		err = -ENODEV;
		goto out;
	}

	if (boot) {
		err = xsc_init_once(dev);
		if (err) {
			xsc_core_err(dev, "xsc_init_once failed %d\n", err);
			goto err_dev_init;
		}
	}

	err = xsc_load(dev);
	if (err) {
		xsc_core_err(dev, "xsc_load failed %d\n", err);
		goto err_load;
	}

	if (boot) {
#ifdef CONFIG_XSC_ESWITCH
		err = xsc_devlink_register(priv_to_devlink(dev), dev->device);
		if (err)
			goto err_devlink_reg;
#endif
	}

	if (dev->pf && dev->pf_id < 2)
		xsc_lag_add_xdev(dev);

	if (xsc_device_registered(dev)) {
		xsc_attach_device(dev);
	} else {
		err = xsc_register_device(dev);
		if (err) {
			xsc_core_err(dev, "register device failed %d\n", err);
			goto err_reg_dev;
		}
	}

	err = xsc_port_ctrl_probe(dev);
	if (err) {
		xsc_core_err(dev, "failed to probe port control node\n");
		goto err_port_ctrl;
	}

	set_bit(XSC_INTERFACE_STATE_UP, &dev->intf_state);
	mutex_unlock(&dev->intf_state_mutex);

	return err;

err_port_ctrl:
	xsc_unregister_device(dev);
err_reg_dev:
	xsc_lag_remove_xdev(dev);
#ifdef CONFIG_XSC_ESWITCH
	if (boot)
		xsc_devlink_unregister(priv_to_devlink(dev));
err_devlink_reg:
#endif
	xsc_unload(dev);

err_load:
	if (boot)
		xsc_cleanup_once(dev);
err_dev_init:
out:
	mutex_unlock(&dev->intf_state_mutex);
	return err;
}

int xsc_unload_one(struct xsc_core_device *dev, bool cleanup)
{
	xsc_port_ctrl_remove(dev);
#ifdef CONFIG_XSC_ESWITCH
	xsc_devlink_unregister(priv_to_devlink(dev));
#endif
	if (cleanup)
		xsc_unregister_device(dev);
	mutex_lock(&dev->intf_state_mutex);
	if (!test_bit(XSC_INTERFACE_STATE_UP, &dev->intf_state)) {
		xsc_core_warn(dev, "%s: interface is down, NOP\n",
			       __func__);
		if (cleanup)
			xsc_cleanup_once(dev);
		goto out;
	}

	clear_bit(XSC_INTERFACE_STATE_UP, &dev->intf_state);
	if (xsc_device_registered(dev))
		xsc_detach_device(dev);

	xsc_unload(dev);

	if (cleanup)
		xsc_cleanup_once(dev);

out:
	mutex_unlock(&dev->intf_state_mutex);

	return 0;
}

static int xsc_pci_probe(struct pci_dev *pci_dev,
			 const struct pci_device_id *id)
{
	struct xsc_core_device *xdev;
	struct xsc_priv *priv;
	int err;
#ifdef CONFIG_XSC_ESWITCH
	struct devlink *devlink;

	devlink = xsc_devlink_alloc();
	if (!devlink) {
		dev_err(&pci_dev->dev, "devlink alloc failed\n");
		return -ENOMEM;
	}
	xdev = devlink_priv(devlink);
#else
	/* allocate core structure and fill it out */
	xdev = kzalloc(sizeof(struct xsc_core_device), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;
#endif

	xdev->pdev = pci_dev;
	xdev->device = &pci_dev->dev;
	priv = &xdev->priv;
	xdev->coredev_type = pci_dev->is_virtfn ?
				XSC_COREDEV_VF : XSC_COREDEV_PF;
	xsc_core_info(xdev, "%s: dev_type=%d is_vf=%d\n",
		__func__, xdev->coredev_type, pci_dev->is_virtfn);

#ifdef CONFIG_XSC_SRIOV
	priv->sriov.probe_vf = probe_vf;
	if (pci_dev->is_virtfn && !probe_vf) {
		xsc_core_err(xdev, "VFs are not binded to xsc driver\n");
		return 0;
	}
#endif

	/* init pcie device */
	pci_set_drvdata(pci_dev, xdev);
	err = xsc_pci_init(xdev, id);
	if (err) {
		xsc_core_err(xdev, "xsc_pci_init failed %d\n", err);
		goto err_pci_init;
	}

#ifdef RUN_WITH_PSV
	if (!xsc_hw_reset) {
		xsc_hw_reset = true;
		err = xsc_start_fw(xdev);
		if (err) {
			xsc_core_err(xdev, "PSV: failed to start fw.\n");
			goto err_start_fw;
		}
	}
#endif

	err = xsc_dev_init(xdev);
	if (err) {
		xsc_core_err(xdev, "xsc_dev_init failed %d\n", err);
		goto err_dev_init;
	}

	if (xsc_fpga_not_supported(xdev)) {
		err = -EOPNOTSUPP;
		goto err_dev_init;
	}

	err = xsc_load_one(xdev, true);
	if (err) {
		xsc_core_err(xdev, "xsc_load_one failed %d\n", err);
		goto err_load;
	}

	return 0;

err_load:
	xsc_dev_cleanup(xdev);
err_dev_init:
#ifdef RUN_WITH_PSV
err_start_fw:
#endif
	xsc_pci_fini(xdev);
err_pci_init:
	pci_set_drvdata(pci_dev, NULL);
#ifdef CONFIG_XSC_ESWITCH
	xsc_devlink_free(devlink);
#else
	kfree(xdev);
#endif
	return err;
}

static void xsc_pci_remove(struct pci_dev *pci_dev)
{
	struct xsc_core_device *xdev = pci_get_drvdata(pci_dev);

	xsc_core_info(xdev, "%s: enter\n", __func__);

	set_bit(XSC_INTERFACE_STATE_TEARDOWN, &xdev->intf_state);
	xsc_unload_one(xdev, true);
	xsc_dev_cleanup(xdev);

	xsc_pci_fini(xdev);
	pci_set_drvdata(pci_dev, NULL);
#ifdef CONFIG_XSC_ESWITCH
	xsc_devlink_free(priv_to_devlink(xdev));
#else
	kfree(xdev);
#endif
}

static struct pci_driver xsc_pci_driver = {
	.name		= "xsc-pci",
	.id_table	= xsc_pci_id_table,
	.probe		= xsc_pci_probe,
	.remove		= xsc_pci_remove,

#ifdef CONFIG_XSC_SRIOV
	.sriov_configure   = xsc_core_sriov_configure,
#endif
};

static int __init xsc_init(void)
{
	int err;

	xsc_register_debugfs();

	err = xsc_port_ctrl_init();
	if (err) {
		pr_err("failed to initialize port control\n");
		goto err_port_ctrl;
	}

	err = xsc_pci_ctrl_init();
	if (err) {
		pr_err("failed to initialize dpdk ctrl\n");
		goto err_pci_ctrl;
	}

	xsc_hw_reset = false;
	err = pci_register_driver(&xsc_pci_driver);
	if (err) {
		pr_err("failed to register pci driver\n");
		goto err_register;
	}

	return 0;

err_register:
	xsc_pci_ctrl_fini();
err_pci_ctrl:
	xsc_port_ctrl_fini();
err_port_ctrl:
	xsc_unregister_debugfs();
	return err;
}

static void __exit xsc_fini(void)
{
	pci_unregister_driver(&xsc_pci_driver);
	xsc_pci_ctrl_fini();
	xsc_port_ctrl_fini();
	xsc_unregister_debugfs();
}

module_init(xsc_init);
module_exit(xsc_fini);

MODULE_DESCRIPTION("Yunsilicon XSC PCI driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
