/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013-2024 Intel Corporation */

#ifndef _IAVF_STATUS_H_
#define _IAVF_STATUS_H_

/* Error Codes */
enum iavf_status {
	IAVF_SUCCESS				= 0,
	IAVF_ERR_NVM				= -1,
	IAVF_ERR_NVM_CHECKSUM			= -2,
	IAVF_ERR_PHY				= -3,
	IAVF_ERR_CONFIG				= -4,
	IAVF_ERR_PARAM				= -5,
	IAVF_ERR_MAC_TYPE			= -6,
	IAVF_ERR_UNKNOWN_PHY			= -7,
	IAVF_ERR_LINK_SETUP			= -8,
	IAVF_ERR_ADAPTER_STOPPED		= -9,
	IAVF_ERR_INVALID_MAC_ADDR		= -10,
	IAVF_ERR_DEVICE_NOT_SUPPORTED		= -11,
	IAVF_ERR_PRIMARY_REQUESTS_PENDING	= -12,
	IAVF_ERR_INVALID_LINK_SETTINGS		= -13,
	IAVF_ERR_AUTONEG_NOT_COMPLETE		= -14,
	IAVF_ERR_RESET_FAILED			= -15,
	IAVF_ERR_SWFW_SYNC			= -16,
	IAVF_ERR_NO_AVAILABLE_VSI		= -17,
	IAVF_ERR_NO_MEMORY			= -18,
	IAVF_ERR_BAD_PTR			= -19,
	IAVF_ERR_RING_FULL			= -20,
	IAVF_ERR_INVALID_PD_ID			= -21,
	IAVF_ERR_INVALID_QP_ID			= -22,
	IAVF_ERR_INVALID_CQ_ID			= -23,
	IAVF_ERR_INVALID_CEQ_ID			= -24,
	IAVF_ERR_INVALID_AEQ_ID			= -25,
	IAVF_ERR_INVALID_SIZE			= -26,
	IAVF_ERR_INVALID_ARP_INDEX		= -27,
	IAVF_ERR_INVALID_FPM_FUNC_ID		= -28,
	IAVF_ERR_QP_INVALID_MSG_SIZE		= -29,
	IAVF_ERR_QP_TOOMANY_WRS_POSTED		= -30,
	IAVF_ERR_INVALID_FRAG_COUNT		= -31,
	IAVF_ERR_QUEUE_EMPTY			= -32,
	IAVF_ERR_INVALID_ALIGNMENT		= -33,
	IAVF_ERR_FLUSHED_QUEUE			= -34,
	IAVF_ERR_INVALID_PUSH_PAGE_INDEX	= -35,
	IAVF_ERR_INVALID_IMM_DATA_SIZE		= -36,
	IAVF_ERR_TIMEOUT			= -37,
	IAVF_ERR_OPCODE_MISMATCH		= -38,
	IAVF_ERR_CQP_COMPL_ERROR		= -39,
	IAVF_ERR_INVALID_VF_ID			= -40,
	IAVF_ERR_INVALID_HMCFN_ID		= -41,
	IAVF_ERR_BACKING_PAGE_ERROR		= -42,
	IAVF_ERR_NO_PBLCHUNKS_AVAILABLE		= -43,
	IAVF_ERR_INVALID_PBLE_INDEX		= -44,
	IAVF_ERR_INVALID_SD_INDEX		= -45,
	IAVF_ERR_INVALID_PAGE_DESC_INDEX	= -46,
	IAVF_ERR_INVALID_SD_TYPE		= -47,
	IAVF_ERR_MEMCPY_FAILED			= -48,
	IAVF_ERR_INVALID_HMC_OBJ_INDEX		= -49,
	IAVF_ERR_INVALID_HMC_OBJ_COUNT		= -50,
	IAVF_ERR_INVALID_SRQ_ARM_LIMIT		= -51,
	IAVF_ERR_SRQ_ENABLED			= -52,
	IAVF_ERR_ADMIN_QUEUE_ERROR		= -53,
	IAVF_ERR_ADMIN_QUEUE_TIMEOUT		= -54,
	IAVF_ERR_BUF_TOO_SHORT			= -55,
	IAVF_ERR_ADMIN_QUEUE_FULL		= -56,
	IAVF_ERR_ADMIN_QUEUE_NO_WORK		= -57,
	IAVF_ERR_BAD_IWARP_CQE			= -58,
	IAVF_ERR_NVM_BLANK_MODE			= -59,
	IAVF_ERR_NOT_IMPLEMENTED		= -60,
	IAVF_ERR_PE_DOORBELL_NOT_ENABLED	= -61,
	IAVF_ERR_DIAG_TEST_FAILED		= -62,
	IAVF_ERR_NOT_READY			= -63,
	IAVF_NOT_SUPPORTED			= -64,
	IAVF_ERR_FIRMWARE_API_VERSION		= -65,
	IAVF_ERR_ADMIN_QUEUE_CRITICAL_ERROR	= -66,
};

#endif /* _IAVF_STATUS_H_ */
