/******************************************************************************
 *                                                                            *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * Bluetooth Uart Driver                                                      *
 *                                                                            *
 ******************************************************************************/
#ifndef __SLSI_BT_LOG_H__
#define __SLSI_BT_LOG_H__

#if IS_ENABLED(CONFIG_KUNIT_SLSI_BT_TEST)
#include "mock/mock_slsi_bt_log.h"
#endif
#ifndef BT_LOG
#include "scsc_mx_module.h"
#include IN_SCSC_INC(scsc_logring.h)
#endif

#define SLSI_BT_LOG_TRACE_DATA_HEX
#define BT_LOG_USING_SCSC_LOG

#if defined(BT_INFO) && !defined(BT_LOG)
#ifdef BT_LOG_USING_SCSC_LOG
#undef BT_INFO
#undef BT_DBG
#undef BT_WARNING
#undef BT_ERR
#else
#define BT_LOG
#endif
#endif /* BT_INFO && !BT_LOG */

/* Logging porting */
#ifndef BT_LOG
#define BT_LOG
#define BT_INFO(...)            SCSC_TAG_INFO(BT_COMMON, __VA_ARGS__)
#define BT_DBG(...)             SCSC_TAG_DEBUG(BT_COMMON, __VA_ARGS__)
#define BT_WARNING(...)	        SCSC_TAG_WARNING(BT_COMMON, __VA_ARGS__)
#define BT_ERR(...)	        SCSC_TAG_ERR(BT_COMMON, __VA_ARGS__)
#endif /* BT_LOG */

#ifndef TR_LOG
#define TR_LOG
#define TR_INFO(...)            SCSC_TAG_INFO(BT_H4, __VA_ARGS__)
#define TR_DBG(...)             SCSC_TAG_DEBUG(BT_H4, __VA_ARGS__)
#define TR_WARNING(...)	        SCSC_TAG_WARNING(BT_H4, __VA_ARGS__)
#define TR_ERR(...)	        SCSC_TAG_ERR(BT_H4, __VA_ARGS__)

/* Tracing Transport elements */
#define TR_SEND(...)            SCSC_TAG_DEBUG(BT_TX, __VA_ARGS__)
#define TR_RECV(...)            SCSC_TAG_DEBUG(BT_RX, __VA_ARGS__)
#define TR_DATA(rx, ...)      (rx) ? TR_RECV(__VA_ARGS__) : TR_SEND(__VA_ARGS__)
#endif /* TR_LOG */

#define BTTR_TAG_USR_TX         (BIT(1))
#define BTTR_TAG_USR_RX         (BIT(1) | BIT(0))
#define BTTR_TAG_H4_TX          (BIT(2))
#define BTTR_TAG_H4_RX          (BIT(2) | BIT(0))
#define BTTR_TAG_BCSP_TX        (BIT(3))
#define BTTR_TAG_BCSP_RX        (BIT(3) | BIT(0))
#define BTTR_TAG_SLIP_TX        (BIT(4))
#define BTTR_TAG_SLIP_RX        (BIT(4) | BIT(0))
#define BTTR_TAG_FASTPATH_TX    (BIT(5))
#define BTTR_TAG_FASTPATH_RX    (BIT(5) | BIT(0))

#define BTTR_TAG_NAME_FILTER    0xFE

#define BTTR_LOG_FILTER_DEFAULT \
	(BTTR_TAG_BCSP_TX | BTTR_TAG_BCSP_RX) | \
	(BTTR_TAG_FASTPATH_TX | BTTR_TAG_FASTPATH_TX)

/* Print data hex */
#ifdef SLSI_BT_LOG_TRACE_DATA_HEX
#define SLSI_BT_LOG_DATA_BUF_MAX	90
void slsi_bt_log_data_hex(const char *func, unsigned int tag,
		const unsigned char *data, size_t len);
#else
#define slsi_bt_log_data_hex(...)
#endif

struct firm_log_filter {
	union {
		unsigned long long             uint64[2];
		unsigned int                   uint32[4];
		struct {
			unsigned int           en0_l;
			unsigned int           en0_h;
			unsigned int           en1_l;
			unsigned int           en1_h;
		};
	};
};

struct firm_log_filter slsi_bt_log_filter_get(void);
void slsi_bt_log_set_transport(void *data);

/* MXLOG_LOG_EVENT_IND HANDLER */
void slsi_bt_mxlog_log_event(const unsigned char len, const unsigned char *);

#if IS_ENABLED(CONFIG_SLSI_BT_FWLOG_SNOOP)
void slsi_bt_fwlog_dump(unsigned char *data, size_t len);
void slsi_bt_fwlog_snoop_queue_tail(const void *queue, bool complete_only);
void slsi_bt_fwlog_snoop_enable(unsigned int filter[4]);
void slsi_bt_fwlog_snoop_disable(void);
#else
#define slsi_bt_fwlog_dump(d, l)
#define slsi_bt_fwlog_snoop_queue_tail(q, c)
#define slsi_bt_fwlog_snoop_enable(f)
#define slsi_bt_fwlog_snoop_disable()
#endif /* CONFIG_SLSI_BT_FW_LOG_TO_SNOOP */

#if IS_ENABLED(CONFIG_SCSC_LOG_COLLECTION)
void bt_hcf_collect_store(void *hcf, size_t size);
void bt_hcf_collect_free(void);
void bt_hcf_collection_request(void);
#else
#define bt_hcf_collect_store(h, s)
#define bt_hcf_collect_free()
#define bt_hcf_collection_request()
#endif

#endif /* __SLSI_BT_LOG_H__ */
