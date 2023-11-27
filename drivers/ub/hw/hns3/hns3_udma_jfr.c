// SPDX-License-Identifier: GPL-2.0
/* Huawei UDMA Linux driver
 * Copyright (c) 2023-2023 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 */

#include <linux/slab.h>
#include "hns3_udma_hem.h"
#include "hns3_udma_cmd.h"
#include "hns3_udma_db.h"
#include "hns3_udma_jfc.h"
#include "hns3_udma_dfx.h"
#include "hns3_udma_jfr.h"

static int init_jfr_cfg(struct udma_dev *dev, struct udma_jfr *jfr,
			struct ubcore_jfr_cfg *cfg)
{
	if (!cfg->max_sge ||
	    cfg->depth > dev->caps.max_srq_wrs ||
	    cfg->max_sge > dev->caps.max_srq_sges) {
		dev_err(dev->dev, "invalid jfr cfg, depth = %u, sge = %u.\n",
			cfg->depth, cfg->max_sge);
		return -EINVAL;
	}

	jfr->wqe_cnt = roundup_pow_of_two(cfg->depth);

	if (cfg->trans_mode == UBCORE_TP_UM)
		/* reserved for UM header */
		jfr->max_sge = roundup_pow_of_two(cfg->max_sge + 1);
	else
		jfr->max_sge = roundup_pow_of_two(cfg->max_sge);

	memcpy(&jfr->ubcore_jfr.jfr_cfg, cfg, sizeof(struct ubcore_jfr_cfg));
	return 0;
}

static int alloc_jfr_idx(struct udma_dev *dev, struct udma_jfr *jfr,
			 struct ubcore_udata *udata, uint64_t addr)
{
	struct udma_jfr_idx_que *idx_que = &jfr->idx_que;
	struct udma_buf_attr buf_attr = {};
	int ret;

	jfr->idx_que.entry_shift = ilog2(UDMA_IDX_QUE_ENTRY_SZ);

	buf_attr.page_shift = dev->caps.idx_buf_pg_sz + PAGE_SHIFT;
	buf_attr.region[0].size =
		to_udma_hem_entries_size(jfr->wqe_cnt,
					 jfr->idx_que.entry_shift);
	buf_attr.region[0].hopnum = dev->caps.idx_hop_num;
	buf_attr.region_count = 1;

	ret = udma_mtr_create(dev, &idx_que->mtr, &buf_attr,
			      dev->caps.idx_ba_pg_sz + PAGE_SHIFT,
			      addr, !!udata);
	if (ret) {
		dev_err(dev->dev,
			"failed to alloc JFR idx mtr, ret = %d.\n", ret);
		return ret;
	}

	idx_que->head = 0;
	idx_que->tail = 0;

	return 0;
}

static void free_jfr_idx(struct udma_dev *dev, struct udma_jfr *jfr)
{
	struct udma_jfr_idx_que *idx_que = &jfr->idx_que;

	return udma_mtr_destroy(dev, &idx_que->mtr);
}

static int alloc_jfr_wqe_buf(struct udma_dev *dev,
			     struct udma_jfr *jfr,
			     struct ubcore_udata *udata, uint64_t addr)
{
	struct udma_buf_attr buf_attr = {};
	int ret;

	jfr->wqe_shift = ilog2(roundup_pow_of_two(UDMA_SGE_SIZE *
						  jfr->max_sge));

	buf_attr.page_shift = dev->caps.srqwqe_buf_pg_sz + PAGE_SHIFT;
	buf_attr.region[0].size = to_udma_hem_entries_size(jfr->wqe_cnt,
							   jfr->wqe_shift);
	buf_attr.region[0].hopnum = dev->caps.srqwqe_hop_num;
	buf_attr.region_count = 1;

	ret = udma_mtr_create(dev, &jfr->buf_mtr, &buf_attr,
			      dev->caps.srqwqe_ba_pg_sz + PAGE_SHIFT,
			      addr, !!udata);
	if (ret)
		dev_err(dev->dev,
			"failed to alloc JFR buf mtr, ret = %d.\n", ret);

	return ret;
}

static void free_jfr_wqe_buf(struct udma_dev *dev, struct udma_jfr *jfr)
{
	udma_mtr_destroy(dev, &jfr->buf_mtr);
}

static int alloc_jfr_buf(struct udma_dev *dev, struct udma_jfr *jfr,
			 struct ubcore_udata *udata)
{
	struct udma_create_jfr_ucmd ucmd = {};
	struct udma_create_jfr_resp resp = {};
	int ret;

	if (udata) {
		ret = copy_from_user(&ucmd, (void *)udata->udrv_data->in_addr,
				     min(udata->udrv_data->in_len,
					 (uint32_t)sizeof(ucmd)));
		if (ret) {
			dev_err(dev->dev,
				"failed to copy JFR udata, ret = %d.\n",
				ret);
			return -EFAULT;
		}
	}

	ret = alloc_jfr_idx(dev, jfr, udata, ucmd.idx_addr);
	if (ret)
		return ret;

	ret = alloc_jfr_wqe_buf(dev, jfr, udata, ucmd.buf_addr);
	if (ret)
		goto err_idx;

	if (dev->caps.flags & UDMA_CAP_FLAG_SRQ_RECORD_DB) {
		ret = udma_db_map_user(dev, ucmd.db_addr, &jfr->db);
		if (ret) {
			dev_err(dev->dev,
				"map jfr db failed, ret = %d.\n", ret);
			goto err_db;
		}
		jfr->jfr_caps |= UDMA_JFR_CAP_RECORD_DB;
	}

	if (udata) {
		resp.jfr_caps = jfr->jfr_caps;
		ret = copy_to_user((void *)udata->udrv_data->out_addr, &resp,
				   min(udata->udrv_data->out_len,
				       (uint32_t)sizeof(resp)));
		if (ret) {
			dev_err(dev->dev,
				"failed to copy jfr resp, ret = %d.\n",
				ret);
			goto err_copy;
		}
	}
	refcount_set(&jfr->refcount, 1);
	init_completion(&jfr->free);
	return 0;

err_copy:
	if (dev->caps.flags & UDMA_CAP_FLAG_SRQ_RECORD_DB) {
		udma_db_unmap_user(dev, &jfr->db);
		jfr->jfr_caps &= ~UDMA_JFR_CAP_RECORD_DB;
	}
err_db:
	free_jfr_wqe_buf(dev, jfr);
err_idx:
	free_jfr_idx(dev, jfr);

	return ret;
}

static int udma_write_jfr_index_queue(struct udma_dev *dev,
				      struct udma_jfr *jfr,
				      struct udma_jfr_context *ctx)
{
#define DMA_IDX_SHIFT 3
	struct udma_jfr_idx_que *idx_que = &jfr->idx_que;
	uint64_t mtts_idx[MTT_MIN_COUNT] = {};
	uint64_t dma_handle_idx = 0;
	int ret;

	/* Get physical address of idx que buf */
	ret = udma_mtr_find(dev, &idx_que->mtr, 0, mtts_idx,
			    ARRAY_SIZE(mtts_idx), &dma_handle_idx);
	if (ret < 1) {
		dev_err(dev->dev, "failed to find mtr for JFR idx, ret = %d.\n",
			ret);
		return -ENOBUFS;
	}

	udma_reg_write(ctx, SRQC_IDX_HOP_NUM,
		       to_udma_hem_hopnum(dev->caps.idx_hop_num, jfr->wqe_cnt));

	udma_reg_write(ctx, SRQC_IDX_BT_BA_L, dma_handle_idx >> DMA_IDX_SHIFT);
	udma_reg_write(ctx, SRQC_IDX_BT_BA_H,
		       upper_32_bits(dma_handle_idx >> DMA_IDX_SHIFT));

	udma_reg_write(ctx, SRQC_IDX_BA_PG_SZ,
		       to_udma_hw_page_shift(idx_que->mtr.hem_cfg.ba_pg_shift));
	udma_reg_write(ctx, SRQC_IDX_BUF_PG_SZ,
		       to_udma_hw_page_shift(idx_que->mtr.hem_cfg.buf_pg_shift));

	udma_reg_write(ctx, SRQC_IDX_CUR_BLK_ADDR_L,
		       to_udma_hw_page_addr(mtts_idx[0]));
	udma_reg_write(ctx, SRQC_IDX_CUR_BLK_ADDR_H,
		       upper_32_bits(to_udma_hw_page_addr(mtts_idx[0])));

	udma_reg_write(ctx, SRQC_IDX_NXT_BLK_ADDR_L,
		       to_udma_hw_page_addr(mtts_idx[1]));
	udma_reg_write(ctx, SRQC_IDX_NXT_BLK_ADDR_H,
		       upper_32_bits(to_udma_hw_page_addr(mtts_idx[1])));

	return 0;
}

static int write_jfrc(struct udma_dev *dev, struct udma_jfr *jfr, void *mb_buf)
{
	struct udma_jfr_context *ctx = (struct udma_jfr_context *)mb_buf;
	uint64_t mtts_wqe[MTT_MIN_COUNT] = {};
	uint64_t dma_handle_wqe = 0;
	int ret;

	memset(ctx, 0, sizeof(*ctx));

	ret = udma_mtr_find(dev, &jfr->buf_mtr, 0, mtts_wqe,
			    ARRAY_SIZE(mtts_wqe), &dma_handle_wqe);
	if (ret < 1) {
		dev_err(dev->dev, "failed to find mtr for JFR WQE, ret = %d.\n",
			ret);
		return -ENOBUFS;
	}

	udma_reg_write(ctx, SRQC_SRQ_ST, 1);
	udma_reg_write(ctx, SRQC_SRQ_TYPE, 0);
	udma_reg_write(ctx, SRQC_PD, 0);
	udma_reg_write(ctx, SRQC_SRQN, jfr->jfrn);
	udma_reg_write(ctx, SRQC_XRCD, 0);
	udma_reg_write(ctx, SRQC_XRC_RSV, 0);
	udma_reg_write(ctx, SRQC_SHIFT, ilog2(jfr->wqe_cnt));
	udma_reg_write(ctx, SRQC_RQWS,
		       jfr->max_sge <= 0 ? 0 : fls(jfr->max_sge - 1));

	udma_reg_write(ctx, SRQC_WQE_HOP_NUM,
		       to_udma_hem_hopnum(dev->caps.srqwqe_hop_num,
					  jfr->wqe_cnt));

	udma_reg_write(ctx, SRQC_WQE_BT_BA_L, dma_handle_wqe >> DMA_WQE_SHIFT);
	udma_reg_write(ctx, SRQC_WQE_BT_BA_H,
		       upper_32_bits(dma_handle_wqe >> DMA_WQE_SHIFT));

	udma_reg_write(ctx, SRQC_WQE_BA_PG_SZ,
		       to_udma_hw_page_shift(jfr->buf_mtr.hem_cfg.ba_pg_shift));
	udma_reg_write(ctx, SRQC_WQE_BUF_PG_SZ,
		       to_udma_hw_page_shift(jfr->buf_mtr.hem_cfg.buf_pg_shift));
	if (jfr->jfr_caps & UDMA_JFR_CAP_RECORD_DB) {
		udma_reg_enable(ctx, SRQC_RECORD_DB_EN);
		udma_reg_write(ctx, SRQC_RECORD_DB_ADDR_L,
			       lower_32_bits(jfr->db.dma) >>
					     DMA_DB_RECORD_SHIFT);
		udma_reg_write(ctx, SRQC_RECORD_DB_ADDR_H,
			       upper_32_bits(jfr->db.dma));
	}

	return udma_write_jfr_index_queue(dev, jfr, ctx);
}

static int udma_hw_create_srq(struct udma_dev *dev,
			      struct udma_cmd_mailbox *mailbox,
			      uint64_t jfrn)
{
	struct udma_cmq_desc desc;
	struct udma_mbox *mb;

	mb = (struct udma_mbox *)desc.data;
	udma_cmq_setup_basic_desc(&desc, UDMA_OPC_POST_MB, false);
	mbox_desc_init(mb, mailbox->dma, 0, jfrn, UDMA_CMD_CREATE_SRQ);

	return udma_cmd_mbox(dev, &desc, UDMA_CMD_TIMEOUT_MSECS, 0);
}

static int alloc_jfrc(struct udma_dev *dev, struct udma_jfr *jfr)
{
	struct udma_jfr_table *jfr_table = &dev->jfr_table;
	struct udma_ida *jfr_ida = &jfr_table->jfr_ida;
	struct udma_cmd_mailbox *mailbox;
	int ret;
	int id;

	id = ida_alloc_range(&jfr_ida->ida, jfr_ida->min, jfr_ida->max,
			     GFP_KERNEL);
	if (id < 0) {
		dev_err(dev->dev, "failed to alloc jfr_id(%d).\n", id);
		return id;
	}
	jfr->jfrn = (uint32_t)id;
	jfr->ubcore_jfr.id = (uint32_t)id;

	ret = udma_table_get(dev, &jfr_table->table, jfr->jfrn);
	if (ret) {
		dev_err(dev->dev, "failed to get JFRC table, ret = %d.\n", ret);
		goto err_ida;
	}

	ret = xa_err(xa_store(&jfr_table->xa, jfr->jfrn, jfr, GFP_KERNEL));
	if (ret) {
		dev_err(dev->dev, "failed to store JFRC, ret = %d.\n", ret);
		goto err_put;
	}

	mailbox = udma_alloc_cmd_mailbox(dev);
	if (IS_ERR_OR_NULL(mailbox)) {
		dev_err(dev->dev, "failed to alloc mailbox for JFRC.\n");
		ret = -ENOMEM;
		goto err_xa;
	}

	ret = write_jfrc(dev, jfr, mailbox->buf);
	if (ret) {
		dev_err(dev->dev, "failed to write JFRC.\n");
		goto err_mbox;
	}

	ret = udma_hw_create_srq(dev, mailbox, jfr->jfrn);
	if (ret) {
		dev_err(dev->dev, "failed to config JFRC, ret = %d.\n", ret);
		goto err_mbox;
	}

	udma_free_cmd_mailbox(dev, mailbox);

	return 0;

err_mbox:
	udma_free_cmd_mailbox(dev, mailbox);
err_xa:
	xa_erase(&jfr_table->xa, jfr->jfrn);
err_put:
	udma_table_put(dev, &jfr_table->table, jfr->jfrn);
err_ida:
	ida_free(&jfr_ida->ida, id);

	return ret;
}

static int udma_hw_destroy_srq(struct udma_dev *dev, uint64_t jfrn)
{
	struct udma_cmq_desc desc;
	struct udma_mbox *mb;

	mb = (struct udma_mbox *)desc.data;
	udma_cmq_setup_basic_desc(&desc, UDMA_OPC_POST_MB, false);
	mbox_desc_init(mb, 0, 0, jfrn, UDMA_CMD_DESTROY_SRQ);

	return udma_cmd_mbox(dev, &desc, UDMA_CMD_TIMEOUT_MSECS, 0);
}

static void store_jfr_id(struct udma_dev *dev, struct udma_jfr *jfr)
{
	struct jfr_list *jfr_new;
	struct jfr_list *jfr_now;
	unsigned long flags;
	spinlock_t *lock;
	int ret;
	int i;

	ret = udma_find_dfx_dev(dev, &i);
	if (ret)
		return;

	jfr_new = kzalloc(sizeof(struct jfr_list), GFP_KERNEL);
	if (jfr_new == NULL)
		return;

	lock = &g_udma_dfx_list[i].dfx->jfr_list->node_lock;
	spin_lock_irqsave(lock, flags);
	list_for_each_entry(jfr_now,
			    &g_udma_dfx_list[i].dfx->jfr_list->node,
			    node) {
		if (jfr_now->jfr_id == jfr->jfrn) {
			jfr_now->jfc_id = jfr->ubcore_jfr.jfr_cfg.jfc->id;
			goto found;
		}
	}

	jfr_new->jfr_id = jfr->jfrn;
	jfr_new->jfc_id = jfr->ubcore_jfr.jfr_cfg.jfc->id;
	list_add(&jfr_new->node, &g_udma_dfx_list[i].dfx->jfr_list->node);
	spin_unlock_irqrestore(lock, flags);

	return;

found:
	spin_unlock_irqrestore(lock, flags);
	kfree(jfr_new);
}

static void delete_jfr_id(struct udma_dev *dev, struct udma_jfr *jfr)
{
	struct jfr_list *jfr_now, *jfr_tmp;
	unsigned long flags;
	spinlock_t *lock;
	int ret;
	int i;

	ret = udma_find_dfx_dev(dev, &i);
	if (ret)
		return;

	lock = &g_udma_dfx_list[i].dfx->jfr_list->node_lock;
	spin_lock_irqsave(lock, flags);
	list_for_each_entry_safe(jfr_now, jfr_tmp,
				 &g_udma_dfx_list[i].dfx->jfr_list->node,
				 node) {
		if (jfr_now->jfr_id == jfr->jfrn) {
			list_del(&jfr_now->node);
			kfree(jfr_now);
			spin_unlock_irqrestore(lock, flags);
			return;
		}
	}
	spin_unlock_irqrestore(lock, flags);
}

static void free_jfrc(struct udma_dev *dev, uint32_t jfrn)
{
	struct udma_jfr_table *jfr_table = &dev->jfr_table;
	int ret;

	ret = udma_hw_destroy_srq(dev, jfrn);
	if (ret)
		dev_err(dev->dev, "destroy failed (%d) for JFRN 0x%06x\n",
			ret, jfrn);

	xa_erase(&jfr_table->xa, jfrn);

	udma_table_put(dev, &jfr_table->table, jfrn);

	ida_free(&jfr_table->jfr_ida.ida, (int)jfrn);
}

static void free_jfr_buf(struct udma_dev *dev, struct udma_jfr *jfr)
{
	if (refcount_dec_and_test(&jfr->refcount))
		complete(&jfr->free);
	wait_for_completion(&jfr->free);

	if (dev->caps.flags & UDMA_CAP_FLAG_SRQ_RECORD_DB)
		udma_db_unmap_user(dev, &jfr->db);
	free_jfr_wqe_buf(dev, jfr);
	free_jfr_idx(dev, jfr);
}

static int udma_modify_jfr_um_qpc(struct udma_dev *dev, struct udma_jfr *jfr,
				  enum udma_qp_state target_state)
{
	union ubcore_tp_attr_mask ubcore_attr_mask;
	struct udma_modify_tp_attr attr = {};
	struct udma_qp *qp = jfr->um_qp;
	int ret;

	attr.sgid_index = qp->qp_attr.eid_index;
	qp->udma_device = dev;
	qp->qp_attr.jfr = jfr;
	qp->recv_jfc = to_udma_jfc(jfr->ubcore_jfr.jfr_cfg.jfc);
	qp->send_jfc = NULL;
	ubcore_attr_mask.value = 0;
	qp->m_attr = &attr;

	ret = udma_modify_qp_common(qp, NULL, ubcore_attr_mask, jfr->um_qp->state, target_state);
	if (ret)
		dev_err(dev->dev, "failed to modify qpc to RTR.\n");

	qp->state = target_state;
	return ret;
}

static int alloc_jfr_um_qp(struct udma_dev *dev, struct udma_jfr *jfr)
{
	struct ubcore_udata udata;
	struct udma_qp *qp;
	int ret;

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	qp->qp_type = QPT_UD;
	qp->qp_attr.is_tgt = true;
	qp->qp_attr.qp_type = QPT_UD;
	qp->qp_attr.qpn_map = &jfr->qpn_map;
	qp->qp_attr.recv_jfc = to_udma_jfc(jfr->ubcore_jfr.jfr_cfg.jfc);
	qp->qp_attr.send_jfc = NULL;
	qp->qp_attr.eid_index =
		to_udma_ucontext(jfr->ubcore_jfr.uctx)->eid_index;
	udata.uctx = NULL;
	ret = udma_create_qp_common(dev, qp, &udata);
	if (ret) {
		dev_err(dev->dev, "failed to create qpc.\n");
		goto failed_create_qpc;
	}
	jfr->um_qp = qp;

	qp->state = QPS_RESET;
	ret = udma_modify_jfr_um_qpc(dev, jfr, QPS_RTR);
	if (ret) {
		dev_err(dev->dev, "failed to modify qpc.\n");
		goto failed_modify_qpc;
	}

	return 0;

failed_modify_qpc:
	udma_destroy_qp_common(dev, qp);
failed_create_qpc:
	kfree(qp);

	return ret;
}

void destroy_jfr_um_qp(struct udma_dev *dev, struct udma_jfr *jfr)
{
	udma_modify_jfr_um_qpc(dev, jfr, QPS_RESET);
	udma_destroy_qp_common(dev, jfr->um_qp);
	kfree(jfr->um_qp);
}

struct ubcore_jfr *udma_create_jfr(struct ubcore_device *dev, struct ubcore_jfr_cfg *cfg,
				   struct ubcore_udata *udata)
{
	struct udma_dev *udma_dev = to_udma_dev(dev);
	struct udma_jfr *jfr;
	int ret;

	jfr = kcalloc(1, sizeof(*jfr), GFP_KERNEL);
	if (!jfr)
		return NULL;

	ret = init_jfr_cfg(udma_dev, jfr, cfg);
	if (ret)
		goto err_alloc_jfr;

	ret = alloc_jfr_buf(udma_dev, jfr, udata);
	if (ret)
		goto err_alloc_jfr;

	ret = alloc_jfrc(udma_dev, jfr);
	if (ret)
		goto err_alloc_buf;

	xa_init(&jfr->tp_table_xa);
	init_jetty_x_qpn_bitmap(udma_dev, &jfr->qpn_map,
				udma_dev->caps.num_jfr_shift,
				UDMA_JFR_QPN_PREFIX, jfr->jfrn);
	if (cfg->trans_mode == UBCORE_TP_UM) {
		jfr->ubcore_jfr.uctx = udata->uctx;
		ret = alloc_jfr_um_qp(udma_dev, jfr);
		if (ret)
			goto err_alloc_jfrc;
	}

	if (dfx_switch)
		store_jfr_id(udma_dev, jfr);

	return &jfr->ubcore_jfr;

err_alloc_jfrc:
	clean_jetty_x_qpn_bitmap(&jfr->qpn_map);
	free_jfrc(udma_dev, jfr->jfrn);
err_alloc_buf:
	free_jfr_buf(udma_dev, jfr);
err_alloc_jfr:
	kfree(jfr);

	return NULL;
}

int udma_destroy_jfr(struct ubcore_jfr *jfr)
{
	struct udma_dev *dev = to_udma_dev(jfr->ub_dev);
	struct udma_jfr *udma_jfr = to_udma_jfr(jfr);

	if (udma_jfr->um_qp)
		destroy_jfr_um_qp(dev, udma_jfr);
	clean_jetty_x_qpn_bitmap(&udma_jfr->qpn_map);

	if (dfx_switch)
		delete_jfr_id(dev, udma_jfr);

	free_jfrc(dev, udma_jfr->jfrn);
	free_jfr_buf(dev, udma_jfr);
	kfree(jfr);

	return 0;
}

struct udma_jfr *get_udma_jfr(struct ubcore_device *dev, uint32_t jfr_id)
{
	struct udma_dev *udma_dev = to_udma_dev(dev);
	struct udma_jfr *udma_jfr;

	udma_jfr = (struct udma_jfr *)xa_load(&udma_dev->jfr_table.xa, jfr_id);
	if (IS_ERR_OR_NULL(udma_jfr)) {
		dev_err(&dev->dev, "failed to find jfr, jfr_id:%u.", jfr_id);
		return NULL;
	}

	return udma_jfr;
}

struct ubcore_tjetty *udma_import_jfr(struct ubcore_device *dev,
				      struct ubcore_tjetty_cfg *cfg,
				      struct ubcore_udata *udata)
{
	struct ubcore_tjetty *tjfr;

	tjfr = kcalloc(1, sizeof(*tjfr), GFP_KERNEL);
	if (!tjfr)
		return NULL;

	return tjfr;
}

int udma_unimport_jfr(struct ubcore_tjetty *tjfr)
{
	kfree(tjfr);

	return 0;
}

static int udma_hw_modify_srq(struct udma_dev *dev, uint32_t jfrn,
			      uint16_t jfr_limit)
{
	struct udma_jfr_context *jfr_context;
	struct udma_jfr_context *jfrc_mask;
	struct udma_cmd_mailbox *mailbox;
	struct udma_cmq_desc desc;
	struct udma_mbox *mb;
	int ret;

	mailbox = udma_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	jfr_context = (struct udma_jfr_context *)mailbox->buf;
	jfrc_mask = (struct udma_jfr_context *)mailbox->buf + 1;
	memset(jfrc_mask, 0xff, sizeof(*jfrc_mask));
	udma_reg_write(jfr_context, SRQC_LIMIT_WL, jfr_limit);
	udma_reg_clear(jfrc_mask, SRQC_LIMIT_WL);

	mb = (struct udma_mbox *)desc.data;
	udma_cmq_setup_basic_desc(&desc, UDMA_OPC_POST_MB, false);
	mbox_desc_init(mb, mailbox->dma, 0, jfrn, UDMA_CMD_MODIFY_SRQC);

	ret = udma_cmd_mbox(dev, &desc, UDMA_CMD_TIMEOUT_MSECS, 0);
	if (ret)
		dev_err(dev->dev, "modify JFR(%u) cmd error(%d).\n",
			jfrn, ret);
	udma_free_cmd_mailbox(dev, mailbox);

	return ret;
}

int udma_modify_jfr(struct ubcore_jfr *jfr, struct ubcore_jfr_attr *attr,
		    struct ubcore_udata *udata)
{
	struct udma_dev *udma_dev = to_udma_dev(jfr->ub_dev);
	struct udma_jfr *udma_jfr = to_udma_jfr(jfr);
	uint32_t jfr_limit;
	int ret;

	if (!(attr->mask & UBCORE_JFR_RX_THRESHOLD)) {
		dev_err(udma_dev->dev, "JFR threshold mask is not set.\n");
		return -EINVAL;
	}

	jfr_limit = attr->rx_threshold;
	if (jfr_limit > udma_jfr->wqe_cnt) {
		dev_err(udma_dev->dev,
			"JFR limit(%u) larger than wqe num(%u).\n",
			jfr_limit, udma_jfr->wqe_cnt);
		return -EINVAL;
	}

	ret = udma_hw_modify_srq(udma_dev, udma_jfr->jfrn, jfr_limit);
	if (ret)
		dev_err(udma_dev->dev,
			"hw modify srq failed, ret = %d.\n", ret);

	return ret;
}

void udma_jfr_event(struct udma_dev *udma_dev, uint32_t jfrn, int event_type)
{
	struct udma_jfr_table *jfr_table = &udma_dev->jfr_table;
	struct ubcore_jfr *ubcore_jfr;
	struct udma_jfr *jfr;
	struct ubcore_event event;

	xa_lock(&jfr_table->xa);
	jfr = (struct udma_jfr *)xa_load(&jfr_table->xa, jfrn);
	xa_unlock(&jfr_table->xa);

	if (!jfr) {
		dev_warn(udma_dev->dev, "Async event for bogus SRQ 0x%08x\n",
			 jfrn);
		return;
	}

	event.event_type = UBCORE_EVENT_JFR_LIMIT_REACHED;

	refcount_inc(&jfr->refcount);
	ubcore_jfr = &jfr->ubcore_jfr;
	if (ubcore_jfr->jfae_handler) {
		event.ub_dev = ubcore_jfr->ub_dev;
		event.element.jfr = ubcore_jfr;
		ubcore_jfr->jfae_handler(&event, ubcore_jfr->uctx);
		dev_info(udma_dev->dev, "Async event for JFR 0x%08x\n", jfrn);
	}

	if (refcount_dec_and_test(&jfr->refcount))
		complete(&jfr->free);
}
