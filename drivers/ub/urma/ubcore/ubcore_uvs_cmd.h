/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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
 * Description: ubcore uvs cmd header file
 * Author: Ji Lei
 * Create: 2023-07-03
 * Note:
 * History: 2023-07-03: Create file
 */

#ifndef UBCORE_UVS_CMD_H
#define UBCORE_UVS_CMD_H

#include <linux/types.h>
#include <linux/uaccess.h>

#include "ubcore_cmd.h"
#include "ubcore_log.h"
#include <urma/ubcore_types.h>
#include "ubcore_priv.h"
#include "ubcore_vtp.h"

#define UBCORE_UVS_CMD_MAGIC 'V'
#define UBCORE_UVS_CMD _IOWR(UBCORE_UVS_CMD_MAGIC, 1, struct ubcore_cmd_hdr)
#define UBCORE_CMD_CHANNEL_INIT_SIZE 32
#define UBCORE_UVS_CMD_DEV_MAX 64
#define UBCORE_MAX_VTP_CFG_CNT 32
#define UBCORE_MAX_EID_CONFIG_CNT 32

/* only for uvs control ubcore device ioctl */
enum ubcore_uvs_cmd {
	UBCORE_CMD_CHANNEL_INIT = 1,
	UBCORE_CMD_CREATE_TPG, /* initiator */
	UBCORE_CMD_CREATE_VTP, /* initiator */
	UBCORE_CMD_MODIFY_TPG,
	UBCORE_CMD_CREATE_TARGET_TPG, /* target */
	UBCORE_CMD_MODIFY_TARGET_TPG, /* target */
	UBCORE_CMD_DESTROY_VTP, /* initiator or target */
	UBCORE_CMD_DESTROY_TPG, /* initiator or target */
	UBCORE_CMD_ADD_SIP,
	UBCORE_CMD_DEL_SIP,
	UBCORE_CMD_MAP_VTP,
	UBCORE_CMD_CREATE_UTP,
	UBCORE_CMD_DESTROY_UTP,
	UBCORE_CMD_GET_DEV_FEATURE,
	UBCORE_CMD_RESTORE_TP_ERROR_RSP,
	UBCORE_CMD_RESTORE_TARGET_TP_ERROR_REQ,
	UBCORE_CMD_RESTORE_TARGET_TP_ERROR_ACK,
	UBCORE_CMD_RESTORE_TP_SUSPEND,
	UBCORE_CMD_CHANGE_TP_TO_ERROR,
	UBCORE_CMD_SET_UPI,
	UBCORE_CMD_SHOW_UPI,
	UBCORE_CMD_SET_GLOBAL_CFG,
	UBCORE_CMD_CONFIG_FUNCTION_MIGRATE_STATE,
	UBCORE_CMD_SET_VPORT_CFG,
	UBCORE_CMD_MODIFY_VTP,
	UBCORE_CMD_GET_DEV_INFO,
	UBCORE_CMD_CREATE_CTP,
	UBCORE_CMD_DESTROY_CTP,
	UBCORE_CMD_CHANGE_TPG_TO_ERROR,
	UBCORE_CMD_LAST
};

struct ubcore_cmd_channel_init {
	struct {
		char userspace_in[UBCORE_CMD_CHANNEL_INIT_SIZE];
	} in;
	struct {
		char kernel_out[UBCORE_CMD_CHANNEL_INIT_SIZE];
	} out;
};

struct ubcore_cmd_tpf {
	enum ubcore_transport_type trans_type;
	struct ubcore_net_addr netaddr;
};

struct ubcore_cmd_tp_cfg {
	union ubcore_tp_cfg_flag flag; /* flag of initial tp */
	/* transaction layer attributes */
	union {
		union ubcore_eid local_eid;
		struct ubcore_jetty_id local_jetty;
	};
	uint16_t fe_idx;
	union {
		union ubcore_eid peer_eid;
		struct ubcore_jetty_id peer_jetty;
	};
	/* tranport layer attributes */
	enum ubcore_transport_mode trans_mode;
	uint8_t retry_num;
	uint8_t retry_factor;      /* for calculate the time slot to retry */
	uint8_t ack_timeout;
	uint8_t dscp;              /* priority */
	uint32_t oor_cnt;          /* OOR window size: by packet */
};

struct ubcore_udrv_ext {
	uint64_t in_addr;
	uint32_t in_len;
	uint64_t out_addr;
	uint32_t out_len;
};

/* create tpg and all tp in the tpg */
struct ubcore_cmd_create_tpg {
	struct {
		struct ubcore_cmd_tpf tpf;
		struct ubcore_tpg_cfg tpg_cfg;
		struct ubcore_cmd_tp_cfg tp_cfg[UBCORE_MAX_TP_CNT_IN_GRP];
	} in;
	struct {
		uint32_t tpgn;
		uint32_t tpn[UBCORE_MAX_TP_CNT_IN_GRP];
	} out;
	/* for alpha */
	struct ubcore_ta_data ta_data;
	enum ubcore_mtu local_mtu;
	struct ubcore_udrv_priv udata;
	struct ubcore_udrv_ext udrv_ext;
};

/* modify tps in the tp list of tpg to RTR, RTS, and then map vtpn to tpg */
struct ubcore_cmd_create_vtp {
	struct {
		struct ubcore_cmd_tpf tpf;
		/* modify tp to RTR */
		uint32_t tpgn;
		struct ubcore_tp_attr rtr_attr[UBCORE_MAX_TP_CNT_IN_GRP];
		union ubcore_tp_attr_mask rtr_mask[UBCORE_MAX_TP_CNT_IN_GRP];
		/* modify tp to RTS */
		/* create vtp */
		struct ubcore_cmd_vtp_cfg vtp;
	} in;
	struct {
		uint32_t rtr_tp_cnt;
		uint32_t rts_tp_cnt;
		uint32_t vtpn;
	} out;
};

/* modify tps in the tp list of tpg to RTR, RTS */
struct ubcore_cmd_modify_tpg {
	struct {
		struct ubcore_cmd_tpf tpf;
		/* modify tp to RTR */
		uint32_t tpgn;
		struct ubcore_tp_attr rtr_attr[UBCORE_MAX_TP_CNT_IN_GRP];
		union ubcore_tp_attr_mask rtr_mask[UBCORE_MAX_TP_CNT_IN_GRP];
		/* modify tp to RTS */
	} in;
	struct {
		uint32_t rtr_tp_cnt;
		uint32_t rts_tp_cnt;
	} out;
	/* for alpha */
	struct ubcore_ta_data ta_data;
	struct ubcore_udrv_ext udrv_ext;
};

/* create tpg, create and modify tps in it to RTR at target */
struct ubcore_cmd_create_target_tpg {
	struct {
		struct ubcore_cmd_tpf tpf;
		/* create tpg and the tps in the tpg */
		struct ubcore_tpg_cfg tpg_cfg;
		struct ubcore_cmd_tp_cfg tp_cfg[UBCORE_MAX_TP_CNT_IN_GRP];
		/* modify tp to RTR */
		struct ubcore_tp_attr rtr_attr[UBCORE_MAX_TP_CNT_IN_GRP];
		union ubcore_tp_attr_mask rtr_mask[UBCORE_MAX_TP_CNT_IN_GRP];
	} in;
	struct {
		uint32_t tpgn;
		uint32_t tpn[UBCORE_MAX_TP_CNT_IN_GRP];
	} out;
	/* for alpha */
	struct ubcore_ta_data ta_data;
	enum ubcore_mtu local_mtu;
	enum ubcore_mtu peer_mtu;
	struct ubcore_udrv_priv udata;
	struct ubcore_udrv_ext udrv_ext;
};

/* modify tps in the tpg to RTS at target */
struct ubcore_cmd_modify_target_tpg {
	struct {
		struct ubcore_cmd_tpf tpf;
		uint32_t tpgn;
	} in;
	struct {
		uint32_t rts_tp_cnt;
	} out;
	/* for alpha */
	struct ubcore_ta_data ta_data;
};

struct ubcore_cmd_destroy_vtp {
	struct {
		struct ubcore_cmd_tpf tpf;
		enum ubcore_transport_mode mode;
		uint32_t local_jetty;
		/* key start */
		union ubcore_eid local_eid;
		union ubcore_eid peer_eid;
		uint32_t peer_jetty;
		/* key end */
	} in;
};

/* modify to error, reset, destroy tps in the tp_list of tpg, then destroy tpg */
struct ubcore_cmd_destroy_tpg {
	struct {
		struct ubcore_cmd_tpf tpf;
		uint32_t tpgn;
	} in;
	struct {
		uint32_t destroyed_tp_cnt; /* the first "destroyed_tp_cnt" tps are destroyed */
	} out;
	/* for alpha */
	struct ubcore_ta_data ta_data;
};

struct ubcore_cmd_opt_sip {
	struct {
		struct ubcore_sip_info info;
	} in;
};

struct ubcore_cmd_map_vtp {
	struct {
		struct ubcore_cmd_tpf tpf;
		/* create vtp */
		struct ubcore_cmd_vtp_cfg vtp;
	} in;
	struct {
		uint32_t vtpn;
	} out;
};

/* create utp */
struct ubcore_cmd_create_utp {
	struct {
		struct ubcore_cmd_tpf tpf;
		struct ubcore_utp_cfg utp_cfg;
		struct ubcore_cmd_vtp_cfg vtp;
		/* todonext: add user data */
	} in;
	struct {
		uint32_t idx;
		uint32_t vtpn;
	} out;
};

/* destroy utp */
struct ubcore_cmd_destroy_utp {
	struct {
		struct ubcore_cmd_tpf tpf;
		uint32_t utp_idx;
		/* todonext: add user data and ext */
	} in;
};

/* create ctp */
struct ubcore_cmd_create_ctp {
	struct {
		struct ubcore_cmd_tpf tpf;
		struct ubcore_ctp_cfg ctp_cfg;
		struct ubcore_cmd_vtp_cfg vtp;
		/* todonext: add user data */
	} in;
	struct {
		uint32_t idx;
		uint32_t vtpn;
	} out;
};

/* destroy ctp */
struct ubcore_cmd_destroy_ctp {
	struct {
		struct ubcore_cmd_tpf tpf;
		uint32_t ctp_idx;
		/* todonext: add user data and ext */
	} in;
};

/* modify vtp */
struct ubcore_cmd_modify_vtp {
	struct {
		struct ubcore_cmd_tpf tpf;
		struct ubcore_cmd_vtp_cfg vtp[UBCORE_MAX_VTP_CFG_CNT];
		uint32_t cfg_cnt;
	} in;
};

/* restore tp error */
struct ubcore_cmd_restore_tp_error {
	struct {
		uint32_t tpgn;
		uint32_t tpn;
		uint16_t data_udp_start;
		uint16_t ack_udp_start;
		uint32_t rx_psn;
		uint32_t tx_psn;
	} in;
};

/* restore tp suspend */
struct ubcore_cmd_restore_tp_suspend {
	struct {
		uint32_t tpgn;
		uint32_t tpn;
		uint16_t data_udp_start;
		uint16_t ack_udp_start;
	} in;
};

/* get sr feature */
struct ubcore_cmd_get_dev_feature {
	struct {
		char dev_name[UBCORE_MAX_DEV_NAME];
	} in;
	struct {
		union ubcore_device_feat feature;
	} out;
};

/* change tp to error */
struct ubcore_cmd_change_tp_to_error {
	struct {
		uint32_t tpgn;
		uint32_t tpn;
	} in;
};

struct ubcore_cmd_set_upi {
	struct {
		char dev_name[UBCORE_UVS_CMD_DEV_MAX];
		uint32_t upi;
	} in;
};

struct ubcore_cmd_show_upi {
	struct {
		char dev_name[UBCORE_UVS_CMD_DEV_MAX];
	} in;
	struct {
		uint32_t upi;
	} out;
};

struct ubcore_cmd_get_dev_info {
	struct {
		char target_tpf_name[UBCORE_UVS_CMD_DEV_MAX];
	} in;
	struct {
		bool port_is_active;
	} out;
};

struct ubcore_cmd_set_global_cfg {
	struct {
		struct ubcore_set_global_cfg global_cfg;
	} in;
};

struct ubcore_cmd_config_function_migrate_state {
	struct {
		uint16_t fe_idx;
		struct ubcore_cmd_tpf tpf;
		struct ubcore_ueid_cfg config[UBCORE_MAX_EID_CONFIG_CNT];
		uint32_t config_cnt;
		enum ubcore_mig_state state;
	} in;
	struct {
		uint32_t cnt;
	} out;
};

struct ubcore_cmd_set_vport_cfg {
	struct {
		struct ubcore_set_vport_cfg vport_cfg;
	} in;
};

struct ubcore_cmd_change_tpg_to_error {
	struct {
		uint32_t tpgn;
		struct ubcore_cmd_tpf tpf;
	} in;
};

int ubcore_uvs_cmd_parse(struct ubcore_cmd_hdr *hdr);

#endif
