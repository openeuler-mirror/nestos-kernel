// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 - 2023, Shanghai Yunsilicon Technology Co., Ltd.
 * All rights reserved.
 */

#include <common/driver.h>
#include <linux/module.h>
#include <common/andes/chip_scale_defines.h>

int xsc_cmd_query_hca_cap(struct xsc_core_device *dev,
			   struct xsc_caps *caps)
{
	struct xsc_cmd_query_hca_cap_mbox_out *out;
	struct xsc_cmd_query_hca_cap_mbox_in in;
	//struct xsc_query_special_ctxs_mbox_out ctx_out;
	//struct xsc_query_special_ctxs_mbox_in ctx_in;
	int err;
	u16 t16;

	out = kzalloc(sizeof(*out), GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	memset(&in, 0, sizeof(in));
	in.hdr.opcode = cpu_to_be16(XSC_CMD_OP_QUERY_HCA_CAP);
	in.hdr.opmod  = cpu_to_be16(0x1);
#ifdef XSC_MSIX_BAR_EMUL
	in.cpu_num = cpu_to_be16(num_online_cpus());
#endif
	err = xsc_cmd_exec(dev, &in, sizeof(in), out, sizeof(*out));
	if (err)
		goto out_out;

	if (out->hdr.status) {
		err = xsc_cmd_status_to_err(&out->hdr);
		goto out_out;
	}

	dev->glb_func_id = be32_to_cpu(out->hca_cap.glb_func_id);
	caps->raweth_qp_id_base = be16_to_cpu(out->hca_cap.raweth_qp_id_base);
	caps->raweth_qp_id_end = be16_to_cpu(out->hca_cap.raweth_qp_id_end);
	caps->raweth_rss_qp_id_base = be16_to_cpu(out->hca_cap.raweth_rss_qp_id_base);
	caps->raw_tpe_qp_num = be16_to_cpu(out->hca_cap.raw_tpe_qp_num);
	caps->max_cqes = 1 << out->hca_cap.log_max_cq_sz;
	caps->max_wqes = 1 << out->hca_cap.log_max_qp_sz;
	caps->max_sq_desc_sz = be16_to_cpu(out->hca_cap.max_desc_sz_sq);
	caps->max_rq_desc_sz = be16_to_cpu(out->hca_cap.max_desc_sz_rq);
	caps->flags = be64_to_cpu(out->hca_cap.flags);
	caps->stat_rate_support = be16_to_cpu(out->hca_cap.stat_rate_support);
	caps->log_max_msg = out->hca_cap.log_max_msg & 0x1f;
	caps->num_ports = out->hca_cap.num_ports & 0xf;
	caps->log_max_cq = out->hca_cap.log_max_cq & 0x1f;
	caps->log_max_eq = out->hca_cap.log_max_eq & 0xf;
	caps->log_max_msix = out->hca_cap.log_max_msix & 0xf;
	caps->max_num_eqs = out->hca_cap.max_num_eqs & 0xff;
	caps->mac_port = out->hca_cap.mac_port & 0xff;
	if (caps->num_ports > XSC_MAX_FW_PORTS) {
		xsc_core_err(dev, "device has %d ports while the driver supports max %d ports\n",
			      caps->num_ports, XSC_MAX_FW_PORTS);
		err = -EINVAL;
		goto out_out;
	}
	caps->send_ds_num = out->hca_cap.send_seg_num;
	caps->send_wqe_shift = out->hca_cap.send_wqe_shift;
	caps->recv_ds_num = out->hca_cap.recv_seg_num;
	caps->recv_wqe_shift = out->hca_cap.recv_wqe_shift;

	caps->embedded_cpu = 0;
	caps->ecpf_vport_exists = 0;
#ifdef CONFIG_XSC_ESWITCH
	caps->eswitch_manager = 1;
	caps->vport_group_manager = 1;
#endif
	caps->log_max_current_uc_list = 0;
	caps->log_max_current_mc_list = 0;
	caps->log_max_vlan_list = 8;
	caps->log_max_qp = out->hca_cap.log_max_qp & 0x1f;
	caps->log_max_mkey = out->hca_cap.log_max_mkey & 0x3f;
	caps->log_max_pd = out->hca_cap.log_max_pd & 0x1f;
	caps->log_max_srq = out->hca_cap.log_max_srqs & 0x1f;
	caps->local_ca_ack_delay = out->hca_cap.local_ca_ack_delay & 0x1f;
	caps->log_max_mcg = out->hca_cap.log_max_mcg;
	caps->log_max_mtt = out->hca_cap.log_max_mtt;
	caps->log_max_tso = out->hca_cap.log_max_tso;
	caps->hca_core_clock = be32_to_cpu(out->hca_cap.hca_core_clock);
	caps->max_rwq_indirection_tables =
		be32_to_cpu(out->hca_cap.max_rwq_indirection_tables);
	caps->max_rwq_indirection_table_size =
		be32_to_cpu(out->hca_cap.max_rwq_indirection_table_size);
	caps->max_qp_mcg = be16_to_cpu(out->hca_cap.max_qp_mcg);
	caps->max_ra_res_qp = 1 << (out->hca_cap.log_max_ra_res_qp & 0x3f);
	caps->max_ra_req_qp = 1 << (out->hca_cap.log_max_ra_req_qp & 0x3f);
	caps->max_srq_wqes = 1 << out->hca_cap.log_max_srq_sz;
	caps->rx_pkt_len_max = be32_to_cpu(out->hca_cap.rx_pkt_len_max);
	caps->max_vfs = be16_to_cpu(out->hca_cap.max_vfs);
	caps->qp_rate_limit_min = be32_to_cpu(out->hca_cap.qp_rate_limit_min);
	caps->qp_rate_limit_max = be32_to_cpu(out->hca_cap.qp_rate_limit_max);

#ifdef MSIX_SUPPORT
	caps->msix_enable = 1;
#else
	caps->msix_enable = 0;
#endif
#ifdef XSC_MSIX_BAR_EMUL
	caps->msix_base = be16_to_cpu(out->hca_cap.msix_base);
	caps->msix_num = be16_to_cpu(out->hca_cap.msix_num);
#endif

	t16 = be16_to_cpu(out->hca_cap.bf_log_bf_reg_size);
	if (t16 & 0x8000) {
		caps->bf_reg_size = 1 << (t16 & 0x1f);
		caps->bf_regs_per_page = XSC_BF_REGS_PER_PAGE;
	} else {
		caps->bf_reg_size = 0;
		caps->bf_regs_per_page = 0;
	}
	caps->min_page_sz = ~(u32)((1 << PAGE_SHIFT) - 1);

	caps->dcbx = 1;
	caps->qos = 1;
	caps->ets = 1;
	caps->dscp = 1;
	caps->max_tc = PRI_NUM;
	caps->log_max_qp_depth = out->hca_cap.log_max_qp_depth & 0xff;

	dev->chip_ver_h = be32_to_cpu(out->hca_cap.chip_ver_h);
	dev->chip_ver_m = be32_to_cpu(out->hca_cap.chip_ver_m);
	dev->chip_ver_l = be32_to_cpu(out->hca_cap.chip_ver_l);
	dev->hotfix_num = be32_to_cpu(out->hca_cap.hotfix_num);
	dev->feature_flag = be32_to_cpu(out->hca_cap.feature_flag);

	memcpy(dev->board_sn, out->hca_cap.board_sn, sizeof(out->hca_cap.board_sn));

	if (xsc_core_is_pf(dev)) {
		dev->regs.tx_db = be64_to_cpu(out->hca_cap.tx_db);
		dev->regs.rx_db = be64_to_cpu(out->hca_cap.rx_db);
		dev->regs.complete_db = be64_to_cpu(out->hca_cap.complete_db);
		dev->regs.complete_reg = be64_to_cpu(out->hca_cap.complete_reg);
		dev->regs.event_db = be64_to_cpu(out->hca_cap.event_db);
	}
	//memset(&ctx_in, 0, sizeof(ctx_in));
	//memset(&ctx_out, 0, sizeof(ctx_out));
	//ctx_in.hdr.opcode = cpu_to_be16(XSC_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	//err = xsc_cmd_exec(dev, &ctx_in, sizeof(ctx_in),
	//			&ctx_out, sizeof(ctx_out));
	//if (err)
	//	goto out_out;

	//if (ctx_out.hdr.status)
	//	err = xsc_cmd_status_to_err(&ctx_out.hdr);

	//caps->reserved_lkey = be32_to_cpu(ctx_out.reserved_lkey);

out_out:
	kfree(out);

	return err;
}

int xsc_cmd_init_hca(struct xsc_core_device *dev)
{
	struct xsc_cmd_init_hca_mbox_in in;
	struct xsc_cmd_init_hca_mbox_out out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(XSC_CMD_OP_INIT_HCA);
	err = xsc_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		err = xsc_cmd_status_to_err(&out.hdr);

	return err;
}

int xsc_cmd_teardown_hca(struct xsc_core_device *dev)
{
	struct xsc_cmd_teardown_hca_mbox_in in;
	struct xsc_cmd_teardown_hca_mbox_out out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(XSC_CMD_OP_TEARDOWN_HCA);
	err = xsc_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		err = xsc_cmd_status_to_err(&out.hdr);

	return err;
}

int xsc_cmd_enable_hca(struct xsc_core_device *dev, u16 vf_idx)
{
	struct xsc_cmd_enable_hca_mbox_in in;
	struct xsc_cmd_enable_hca_mbox_out out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(XSC_CMD_OP_ENABLE_HCA);
	in.pf = dev->pf;
	in.pcie = dev->pcie;
	in.pf_id = dev->pf_id;
	in.vf_id = cpu_to_be16(vf_idx);
	err = xsc_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		err = xsc_cmd_status_to_err(&out.hdr);

	return err;
}

int xsc_cmd_disable_hca(struct xsc_core_device *dev, u16 vf_idx)
{
	struct xsc_cmd_disable_hca_mbox_in in;
	struct xsc_cmd_disable_hca_mbox_out out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(XSC_CMD_OP_DISABLE_HCA);
	in.pf = dev->pf;
	in.pcie = dev->pcie;
	in.pf_id = dev->pf_id;
	in.vf_id = cpu_to_be16(vf_idx);
	err = xsc_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		err = xsc_cmd_status_to_err(&out.hdr);

	return err;
}

static u32 xsc_board_num;
static char xsc_board_sn[MAX_BOARD_NUM][XSC_BOARD_SN_LEN];

int xsc_get_board_id(struct xsc_core_device *dev)
{
	int i = 0;

	xsc_core_info(dev, "board_sn=%s, current_board_num=%d\n",
			dev->board_sn, xsc_board_num);
	if (strnlen(dev->board_sn, XSC_BOARD_SN_LEN) == 0)
		return 0;

	for (i = 0; i < xsc_board_num; i++) {
		if (!strncmp(xsc_board_sn[i], dev->board_sn, XSC_BOARD_SN_LEN)) {
			dev->board_id = i;
			return 0;
		}
	}
	if (xsc_board_num < MAX_BOARD_NUM) {
		memcpy(xsc_board_sn[xsc_board_num], dev->board_sn, XSC_BOARD_SN_LEN);
		dev->board_id = xsc_board_num++;
	} else {
		return -ENOMEM;
	}

	return 0;
}
