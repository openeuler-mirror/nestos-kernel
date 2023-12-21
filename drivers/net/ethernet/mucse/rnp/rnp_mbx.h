/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022 - 2023 Mucse Corporation. */

#ifndef _RNP_MBX_H_
#define _RNP_MBX_H_

#include "rnp_type.h"

#define RNP_VFMAILBOX_SIZE 14 /* 16 32 bit words - 64 bytes */
#define RNP_ERR_MBX -100

#define RNP_VT_MSGTYPE_ACK 0x80000000
/* Messages below or'd with */
/* this are the ACK */
#define RNP_VT_MSGTYPE_NACK 0x40000000
/* Messages below or'd with
 * this are the NACK
 */
#define RNP_VT_MSGTYPE_CTS 0x20000000
/* Indicates that VF is still
 * clear to send requests
 */
#define RNP_VT_MSGINFO_SHIFT 14
/* bits 23:16 are used for exra info for certain messages */
#define RNP_VT_MSGINFO_MASK (0x7F << RNP_VT_MSGINFO_SHIFT)
/* VLAN pool filtering masks */
#define RNP_VLVF_VIEN 0x80000000 /* filter is valid */
#define RNP_VLVF_ENTRIES 64
#define RNP_VLVF_VLANID_MASK 0x00000FFF
#define RNP_VNUM_OFFSET (21)
#define RNP_VF_MASK (0x7f << 21)
#define RNP_MAIL_CMD_MASK 0x3fff
/* mailbox API, legacy requests */
#define RNP_VF_RESET 0x01 /* VF requests reset */
#define RNP_VF_SET_MAC_ADDR 0x02 /* VF requests PF to set MAC addr */
#define RNP_VF_SET_MULTICAST 0x03 /* VF requests PF to set MC addr */
#define RNP_VF_SET_VLAN 0x04 /* VF requests PF to set VLAN */

/* mailbox API, version 1.0 VF requests */
//#define RNP_VF_SET_LPE 0x05 /* VF requests PF to set VMOLR.LPE */
#define RNP_VF_SET_MACVLAN 0x06 /* VF requests PF for unicast filter */
#define RNP_VF_GET_MACADDR 0x07 /* get vf macaddr */
#define RNP_VF_API_NEGOTIATE 0x08 /* negotiate API version */

/* mailbox API, version 1.1 VF requests */
#define RNP_VF_GET_QUEUES 0x09 /* get queue configuration */
#define RNP_VF_SET_VLAN_STRIP 0x0a /* VF requests PF to set VLAN STRIP */
#define RNP_VF_REG_RD 0x0b /* vf read reg */
#define RNP_VF_GET_MTU 0x0c /* vf get pf ethtool setup */
#define RNP_VF_SET_MTU 0x0d /* vf get pf ethtool setup */
#define RNP_VF_GET_FW 0x0e /* vf get firmware version */
#define RNP_VF_GET_LINK 0x10 /* get link status */
#define RNP_VF_RESET_PF 0x11
#define RNP_VF_GET_DMA_FRAG 0x12
#define RNP_PF_SET_FCS 0x10 /* PF set fcs status */
#define RNP_PF_SET_PAUSE 0x11 /* PF set pause status */
#define RNP_PF_SET_FT_PADDING 0x12 /* PF set ft padding status */
#define RNP_PF_SET_VLAN_FILTER 0x13 /* PF set ntuple status */
#define RNP_PF_SET_VLAN 0x14 /* PF set ntuple status */
#define RNP_PF_SET_LINK 0x15 /* PF set ntuple status */
#define RNP_PF_SET_MTU 0x16 /* PF set ntuple status */
#define RNP_PF_SET_RESET 0x17 /* PF set ntuple status */
#define RNP_PF_LINK_UP (1 << 31)
#define RNP_PF_REMOVE 0x0f
/* GET_QUEUES return data indices within the mailbox */
#define RNP_VF_TX_QUEUES 1 /* number of Tx queues supported */
#define RNP_VF_RX_QUEUES 2 /* number of Rx queues supported */
#define RNP_VF_TRANS_VLAN 3 /* Indication of port vlan */
#define RNP_VF_DEF_QUEUE 4 /* Default queue offset */
#define RNP_VF_QUEUE_START 5 /* Default queue offset */
#define RNP_VF_QUEUE_DEPTH 6 /* ring depth */
/* length of permanent address message returned from PF */
#define RNP_VF_PERMADDR_MSG_LEN 11
/* word in permanent address message with the current multicast type */
#define RNP_VF_MC_TYPE_WORD 3
#define RNP_VF_DMA_VERSION_WORD 4
#define RNP_VF_VLAN_WORD 5
#define RNP_VF_PHY_TYPE_WORD 6
#define RNP_VF_FW_VERSION_WORD 7
#define RNP_VF_LINK_STATUS_WORD 8
#define RNP_VF_AXI_MHZ 9
#define PF_FEATRURE_VLAN_FILTER BIT(0)
#define RNP_VF_FEATURE 10
#define RNP_PF_CONTROL_PRING_MSG 0x0100 /* PF control message */
#define RNP_VF_MBX_INIT_TIMEOUT 2000 /* number of retries on mailbox */
#define RNP_VF_MBX_INIT_DELAY 500 /* microseconds between retries */

enum MBX_ID {
	MBX_VF0 = 0,
	MBX_VF1,
	MBX_VF2,
	MBX_VF3,
	MBX_VF4,
	MBX_VF5,
	MBX_VF6,
	MBX_VF7,
	MBX_VF8,
	MBX_VF9,
	MBX_VF10,
	MBX_VF11,
	MBX_VF12,
	MBX_VF13,
	MBX_VF14,
	MBX_VF15,
	MBX_VF16,
	MBX_VF17,
	MBX_VF18,
	MBX_VF19,
	MBX_VF20,
	MBX_VF21,
	MBX_VF22,
	MBX_VF23,
	MBX_VF24,
	MBX_VF25,
	MBX_VF26,
	MBX_VF27,
	MBX_VF28,
	MBX_VF29,
	MBX_VF30,
	MBX_VF31,
	MBX_VF32,
	MBX_VF33,
	MBX_VF34,
	MBX_VF35,
	MBX_VF36,
	MBX_VF37,
	MBX_VF38,
	MBX_VF39,
	MBX_VF40,
	MBX_VF41,
	MBX_VF42,
	MBX_VF43,
	MBX_VF44,
	MBX_VF45,
	MBX_VF46,
	MBX_VF47,
	MBX_VF48,
	MBX_VF49,
	MBX_VF50,
	MBX_VF51,
	MBX_VF52,
	MBX_VF53,
	MBX_VF54,
	MBX_VF55,
	MBX_VF56,
	MBX_VF57,
	MBX_VF58,
	MBX_VF59,
	MBX_VF60,
	MBX_VF61,
	MBX_VF62,
	MBX_VF63,
	MBX_CM3CPU,
	MBX_FW = MBX_CM3CPU,
	MBX_VFCNT
};

enum PF_STATUS {
	PF_FCS_STATUS,
	PF_PAUSE_STATUS,
	PF_FT_PADDING_STATUS,
	PF_VLAN_FILTER_STATUS,
	PF_SET_VLAN_STATUS,
	PF_SET_LINK_STATUS,
	PF_SET_MTU,
	PF_SET_RESET,
};

s32 rnp_read_mbx(struct rnp_hw *hw, u32 *msg, u16 size, enum MBX_ID);
s32 rnp_write_mbx(struct rnp_hw *hw, u32 *msg, u16 size, enum MBX_ID);
s32 rnp_check_for_msg(struct rnp_hw *hw, enum MBX_ID);
s32 rnp_check_for_ack(struct rnp_hw *hw, enum MBX_ID);
s32 rnp_check_for_rst(struct rnp_hw *hw, enum MBX_ID);
s32 rnp_init_mbx_params_pf(struct rnp_hw *hw);
unsigned int rnp_mbx_change_timeout(struct rnp_hw *hw, int timeout_ms);
extern struct rnp_mbx_operations mbx_ops_generic;

#endif /* _RNP_MBX_H_ */
