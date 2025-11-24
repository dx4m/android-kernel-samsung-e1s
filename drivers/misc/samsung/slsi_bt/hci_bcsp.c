/******************************************************************************
 *                                                                            *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * Bluetooth HCI Three-Wire Uart Transport Layer & BCSP                       *
 * (SIG Core v5.3 - Vol4. PartD and BlueCore Serial Protocol 2004 by CSR)     *
 *                                                                            *
 ******************************************************************************/
#include <linux/skbuff.h>
#include <linux/crc-ccitt.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/bitrev.h>
#include <linux/seq_file.h>
#include <linux/delay.h>

#include "scsc_mx_module.h"
#include IN_SCSC_INC(scsc_wakelock.h)

#include "slsi_bt_err.h"
#include "hci_trans.h"
#include "hci_pkt.h"
#include "hci_bcsp.h"
#include "slsi_bt_fastpath.h"

#ifndef FALL_THROUGH
#define FALL_THROUGH() __attribute__((__fallthrough__))
#endif

/* BCSp context */
static struct hci_bcsp {
	struct hci_trans                *htr;

	struct work_struct              work_tx;
	struct work_struct              work_timeout;

	struct mutex                    lock;
	struct scsc_wake_lock           tx_wake_lock;

	struct sk_buff_head             rel_q;
	struct sk_buff_head             sent_q;	        /* waiting ack */
	struct sk_buff_head             unrel_q;

	struct sk_buff                  *collect_skb;      /* collector */

	struct timer_list               resend_timer;
	struct timer_list               wakeup_timer;
	int                             wakeup_timeout_cnt;

	int                             resend_cnt;
	int                             ack_mismatch;

	enum bcsp_link_state {
		HCI_BCSP_LINK_UNINITITALIZED = 0,
		HCI_BCSP_LINK_INITIALIZED,
		HCI_BCSP_LINK_WAIT_PEER_ACTIVE,
		HCI_BCSP_LINK_ACTIVE,
		HCI_BCSP_LINK_LOW_POWER,
	} state, peer_state;
	unsigned char                   cfg;
	unsigned char                   peer_cfg;

	unsigned char                   seq;       /* Sequence number */
	unsigned char                   ack;       /* Acknowledgement number */
	unsigned char                   rack;      /* Received ack number */
	bool                            use_crc;
	unsigned char                   window;    /* Window size */
	unsigned char                   credit;
	bool                            sw_fctrl;  /* OOF Flow Control */
	bool                            ack_req;

	unsigned long                   flags;
} slsi_bt_bcsp;

/* BCSP flag bits */
#define HCI_BCSP_OPENED        0
#define HCI_BCSP_TX_WAKEUP     1
#define HCI_BCSP_SENDING       2

static void bcsp_set_resend_timeout(struct hci_bcsp *bcsp)
{
	if (test_bit(HCI_BCSP_OPENED, &bcsp->flags))
		mod_timer(&bcsp->resend_timer, HCI_BCSP_RESEND_TIMEOUT);
}

static void bcsp_set_wakeup_timeout(struct hci_bcsp *bcsp, unsigned long expires)
{
	if (test_bit(HCI_BCSP_OPENED, &bcsp->flags))
		mod_timer(&bcsp->wakeup_timer, expires);
}

/* skbs */
#define BCSP_HEADER_SIZE        (4)
#define BCSP_PACKET_SIZE_MAX    (4+ 4095 + 2)
#define SLIP_PAYLOAD_SIZE_MAX   (BCSP_PACKET_SIZE_MAX*2 + 2)
static inline struct sk_buff *alloc_hci_bcsp_pkt_skb(const int size)
{
	/* Size includes 2 bytes of crc and 2 bytes of delimiters for slip.
	 * The packet uses 4 bytes of BCSP header
	 */
	struct sk_buff *skb = __alloc_hci_pkt_skb(size+2+2, BCSP_HEADER_SIZE);
	if (skb)
		SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_BCSP);
	else
		BT_WARNING("allocation failed\n");
	return skb;
}

static inline struct sk_buff *alloc_hci_slip_pkt_skb(const int size)
{
	/* Size includes 2 bytes of crc and 2 bytes of delimiters for slip.
	 * The packet uses 4 bytes of BCSP header and 1 bytes of Start slip
	 * oct.
	 */
	struct sk_buff *skb = __alloc_hci_pkt_skb(size+2+2, 5);
	if (skb)
		SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_SLIP);
	else
		BT_WARNING("allocation failed\n");
	return skb;
}

static inline struct hci_bcsp *get_hci_bcsp(const struct hci_trans *htr)
{
	return htr ? (struct hci_bcsp *)htr->tdata : NULL;
}

/********************************** SLIP **************************************/

static inline void slip_put_c0(struct sk_buff *skb)
{
	const unsigned char oct_c0 = 0xc0;
	skb_put_data(skb, &oct_c0, 1);
}

static inline int slip_special_oct_search(const unsigned char *data, int len,
		bool sw_fctrl)
{
	int offset = 0;
	while (offset < len) {
		switch (data[offset]) {
		case 0x11:
		case 0x13:
			if (!sw_fctrl)
				break;
		FALL_THROUGH();
		case 0xc0:
		case 0xdb:
			return offset;
		}
		offset++;
	}
	return len;
}

static inline int slip_esc_oct_search(const unsigned char *data, int len)
{
	int offset = 0;
	while (offset < len - 1) {
		if (data[offset] == 0xdb)
			return offset;
		offset++;
	}
	return len;
}

static struct sk_buff *slip_encode(const unsigned char *data, int len,
		bool sw_fctrl)
{
	struct sk_buff *skb;
	const char esc_c0[2] = { 0xdb, 0xdc }, esc_db[2] = { 0xdb, 0xdd },
		esc_11[2] = { 0xdb, 0xde }, esc_13[2] = { 0xdb, 0xdf };
	int offset = 0;

	/* SLIP packet needs to double size of buffer for the worst case.
	 * example) all packets are c0
	 */
	skb = alloc_hci_slip_pkt_skb(len * 2);
	if (skb == NULL)
		return NULL;

	slip_put_c0(skb);

	while (len > 0) {
		offset = slip_special_oct_search(data, len, sw_fctrl);
		skb_put_data(skb, data, offset);
		if (len == offset)
			break;          /* Complete. No more special oct. */

		data += offset;

		switch ((unsigned char)*data) {
		case 0x11: /* 0x11 and 0x13 are using when sw_fctrl == true */
			skb_put_data(skb, esc_11, 2);
			break;
		case 0x13:
			skb_put_data(skb, esc_13, 2);
			break;
		case 0xc0:
			skb_put_data(skb, esc_c0, 2);
			break;
		case 0xdb:
			skb_put_data(skb, esc_db, 2);
			break;
		default:
			skb_put_data(skb, data, 1);
		}
		data++;
		len -= (offset + 1);
	}

	slip_put_c0(skb);
	return skb;
}

static struct sk_buff *slip_decode(const unsigned char *data, int len)
{
	const char c0 = 0xc0, db = 0xdb, s11 = 0x11, s13 = 0x13;
	struct sk_buff *skb;
	int esc_offset = 0;

	skb = alloc_hci_bcsp_pkt_skb(len);
	if (skb == NULL)
		return NULL;

	/* Remove start c0 and end c0. 2bytes */
	data++;
	len -= 2;

	while (len > 0) {
		esc_offset = slip_esc_oct_search(data, len);
		skb_put_data(skb, data, esc_offset);
		if (len == esc_offset)
			break;          /* Complete. No more esc oct */

		data += esc_offset + 1;
		switch((unsigned char)*data) {
		case 0xdc:
			skb_put_data(skb, &c0, 1);
			break;
		case 0xdd:
			skb_put_data(skb, &db, 1);
			break;
		case 0xde:
			skb_put_data(skb, &s11, 1);
			break;
		case 0xdf:
			skb_put_data(skb, &s13, 1);
			break;
		default:
			// It's not SLIP packet
			esc_offset -= 1;
			data--;
			skb_put_data(skb, data, 1);
		}

		data++;
		len -= (esc_offset + 2);
	}

	return skb;
}

static struct sk_buff *slip_get_skb_enough_room(struct sk_buff *skb, int count)
{
	struct sk_buff *nskb = NULL;

	if (skb_tailroom(skb) > count)
		return skb;
	else if (skb->len+count < SLIP_PAYLOAD_SIZE_MAX) {
		int expand = SLIP_PAYLOAD_SIZE_MAX - skb->len - skb_tailroom(skb);

		TR_DBG("expand skb to %d\n", SLIP_PAYLOAD_SIZE_MAX);
		nskb = skb_copy_expand(skb, 0, expand, GFP_ATOMIC);
		if (nskb == NULL)
			BT_WARNING("failed to skb expand\n");
	} else
		TR_ERR("Overflow SLIP packet. skb_tailroom=%d, count=%d\n",
					     skb_tailroom(skb), count);
	kfree_skb(skb);
	return nskb;
}

static int slip_collect_pkt(struct sk_buff **ref_skb, const unsigned char *data,
		int count)
{
	struct sk_buff *skb = NULL;
	int s = 0, e = 0;

	if (ref_skb && data)
		skb = *ref_skb;
	else
		return -EINVAL;

	if (skb == NULL) {
		/* find SLIP starter */
		while (s < count && data[s] != 0xc0) s++;
		if (s > 0)
			TR_WARNING("skip invalid bytes: %d\n", s);
		if (s < count) {
			skb = alloc_hci_slip_pkt_skb(count - s);
			if (skb)
				e = s+1;
			else
				return -ENOMEM;
		} else
			return -EINVAL;
	}

	/* find SLIP end. All SLIP packets start and end with 0xc0 */
	while (e < count) {
		if (data[e++] == 0xc0) {
			SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_BCSP);
			break;
		}
	}

	skb = slip_get_skb_enough_room(skb, e-s);
	if (skb == NULL) {
		slsi_bt_err(SLSI_BT_ERR_BCSP_FAIL);
		*ref_skb = NULL;
		return -ENOMEM;
	}

	skb_put_data(skb, data + s, e - s);
	if (GET_HCI_PKT_TR_TYPE(skb) == HCI_TRANS_BCSP && skb->len == 2) {
		TR_DBG("Continous c0: throw the first away\n");
		kfree_skb(skb);
		*ref_skb = NULL;
		return e - 1;
	} else if (GET_HCI_PKT_TR_TYPE(skb) != HCI_TRANS_BCSP) {
		TR_DBG("collected len: %u\n", skb->len);
	}

	*ref_skb = skb;
	return e;
}

static inline bool slip_check_complete(struct sk_buff *skb)
{
	if (GET_HCI_PKT_TR_TYPE(skb) != HCI_TRANS_BCSP)
		return false;
	if (skb->data[0] != 0xc0 || skb->data[skb->len-1] != 0xc0)
		return false;
	return true;
}

/*******************************************************************************
 * Fastpath for H5.
 * Fastpath is a S.LSI specific functionality which is defined in SC-511108-SP.
 * The packets for Fastpath is transmitted by VSPMP which is defined in
 * SC-5111114-SP
 ******************************************************************************/
static void fastpath_enable(struct hci_bcsp *bcsp, bool enable)
{
	struct sk_buff *skb = NULL;

	if (slsi_bt_fastpath_supported()) {
		BT_INFO("fastpath enabled: %s\n", enable ? "true" : "false");
		skb = slsi_bt_fastpath_enable_req_packet(enable);
		if (skb)
			skb_queue_tail(&bcsp->rel_q, skb);
		else
			BT_WARNING("FASTPATH_ENABLE_REQ_PACKET is not sent");
	}
}

static int fastpath_control_packet_handle(struct hci_bcsp *bcsp,
					  const char *data, size_t len)
{
	struct fastpath_datagram datagram;
	struct sk_buff *rsp = NULL, *alter = NULL;
	int pos = 0;

	/* skip vspmp channel byte */
	data++;
	len--;

	while (len > pos) {
		unsigned char tag = data[pos++];
		unsigned char length = data[pos++];
		const char *value = &data[pos];
		pos += length;

		switch (tag) {
		case FASTPATH_ENABLE_CFM_TAG:
			BT_INFO("fastpath enable cfm: %u\n", *value);
			break;

		case FASTPATH_TO_AIR_ACTIVATE_IND_TAG:
			BT_INFO("fastpath to air activate ind: %u\n", *value);
			slsi_bt_fastpath_activate((bool)*value);

			rsp = slsi_bt_fastpath_to_air_activate_rsp_packet();
			if (rsp == NULL) {
				BT_ERR("failed to alloc_hci_pkt_skb\n");
				return -ENOMEM;
			}
			skb_queue_tail(&bcsp->rel_q, rsp);
			break;

		case FASTPATH_DATAGRAM_TAG:
			BT_DBG("fastpath datagram: %u\n", value[0]);
			slsi_bt_fastpath_bytes_to_datagram(&datagram, value);
			alter = slsi_bt_fastpath_read(&datagram);

			hci_trans_recv_skb(bcsp->htr, alter);
			break;

		default:
			BT_ERR("Unsupported tag is received: %u\n", tag);
			break;
		}
	}

	return pos;
}

static struct sk_buff *change_to_fastpath(struct sk_buff *skb)
{
	struct fastpath_datagram datagram;
	int err;

	BT_DBG("send to fastpath\n");
	err = slsi_bt_fastpath_write(skb, &datagram);
	kfree_skb(skb);
	if (err) {
		BT_ERR("failed to slsi_bt_fastpath_write: %d\n", err);
		return NULL;
	}

	skb = slsi_bt_fastpath_datagram_packet(&datagram);
	if (skb == NULL) {
		BT_ERR("failed to allocate_hci_pkt_skb\n");
		return NULL;
	}
	BT_DBG("FASTPATH_DATAGRAM\n");
	return skb;
}

static void bcsp_update_fastpath_infor(struct sk_buff *skb, int key)
{
	if (GET_HCI_PKT_TYPE(skb) != HCI_BCSP_TYPE_VENDOR)
		return;

	slsi_bt_fastpath_update_key(skb->data + BCSP_HEADER_SIZE,
				    skb->len + BCSP_HEADER_SIZE, key);
}

/********************************** BCSP Packet *******************************/
static inline bool bcsp_cross_scenario_detected(struct hci_bcsp *bcsp)
{
	/* The cross scenario is a case in which a packet is sending when bt
	 * controller enters a low power mode. In this case, the resending
	 * routine is performed quickly to improve performance.
	 * This function should always be used with low power concept.
	 */
	return bcsp->seq != bcsp->rack;
}
static void bcsp_reload_unacked_packets(struct hci_bcsp *bcsp);

static void bcsp_skb_put_crc(struct sk_buff *skb)
{
	unsigned int crc = 0;

	/* Remove first, if skb has crc bytes */
	if (GET_HCI_PKT_TR_TYPE(skb) == HCI_TRANS_BCSP)
		skb_trim(skb, skb->len - 2);

	crc = crc_ccitt(0xffff, skb->data, skb->len);

	/* The MSB should be transmitted first, but UART transmits LSB first.
	 * reverse it.
	 */
	crc = bitrev16(crc);

	skb_put_u8(skb, (crc >> 8) & 0x00ff);
	skb_put_u8(skb, crc & 0x00ff);
}

static bool bcsp_skb_check_crc(struct sk_buff *skb)
{
	unsigned int crc = (skb->data[skb->len-2] << 8)
			 | (skb->data[skb->len-1]);

	return bitrev16(crc) == crc_ccitt(0xffff, skb->data, skb->len-2);
}

static inline bool bcsp_check_reliable(const unsigned char type)
{
	/* BCSP specification uses different packet type for Link Establish */
	switch (type) {
	case HCI_BCSP_TYPE_ACL:
	case HCI_BCSP_TYPE_CMD:
	case HCI_BCSP_TYPE_ISO:
	case HCI_BCSP_TYPE_SCO:
	case HCI_BCSP_TYPE_VENDOR:
		return true;
	case HCI_BCSP_TYPE_ACK:
	case HCI_BCSP_TYPE_LINK_CONTROL:
	default:
		return false;
	}
	return false;
}

static bool bcsp_skb_check_data_integrity_option(struct sk_buff *skb)
{
	switch (GET_HCI_PKT_TYPE(skb)) {
	case HCI_BCSP_TYPE_ACK:
	case HCI_BCSP_TYPE_LINK_CONTROL:
		return false;
	default:
		return true;
	}
}

static void bcsp_skb_update_packet(struct hci_bcsp *bcsp, struct sk_buff *skb)
{
	char type = GET_HCI_PKT_TYPE(skb);
	unsigned char hdr[4] = { 0, };
	unsigned char crc = bcsp->use_crc;
	size_t plen = 0;

	if (GET_HCI_PKT_TR_TYPE(skb) == HCI_TRANS_HCI) {
		plen = skb->len;
		skb_push(skb, sizeof(hdr));
	} else if (GET_HCI_PKT_TR_TYPE(skb) == HCI_TRANS_BCSP) {
		/* Do not add CRC bytes to payload length */
		if ((skb->data[0] >> 6) & 0x1)
			plen = skb->len - sizeof(hdr)-2;
	}

	if (bcsp_check_reliable(type)) {
		TR_DBG("reliable packet. seq=%u\n", bcsp->seq);
		hdr[0] = (bcsp->seq & 0x7) | (0x1 << 7);
		bcsp->seq = (bcsp->seq + 1) & 0x7;

		bcsp_update_fastpath_infor(skb, bcsp->seq);
	}

	if (!bcsp_skb_check_data_integrity_option(skb))
		crc = 0;

	hdr[0] |= ((bcsp->ack & 0x7) << 3) | ((crc & 0x1) << 6);
	hdr[1] = ((plen & 0xF) << 4) | (type & 0xF);
	hdr[2] = plen >> 4;
	hdr[3] = ~(hdr[0] + hdr[1] + hdr[2]) & 0xff;
	memcpy(skb->data, hdr, sizeof(hdr));

	if (crc)
		bcsp_skb_put_crc(skb);

	SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_BCSP);
}

enum {
	/* Link establishment */
	BCSP_LINK_MSG_UNKNOWN,
	BCSP_LINK_MSG_SYNC,
	BCSP_LINK_MSG_SYNC_RSP,
	BCSP_LINK_MSG_CONF,
	BCSP_LINK_MSG_CONF_RSP,

	/* Low power */
	BCSP_LINK_MSG_WAKEUP,
	BCSP_LINK_MSG_WOKEN,
	BCSP_LINK_MSG_SLEEP,
	BCSP_LINK_MSG_MAX,
};

static char *message_to_str[BCSP_LINK_MSG_SLEEP+1] = {
	"UNKNOWN",
	"SYNC",
	"SYNC_RSP",
	"CONF",
	"CONF_RSP",
	"WAKEUP",
	"WOKEN",
	"SLEEP" };

static char *state_to_str[HCI_BCSP_LINK_LOW_POWER+1] = {
	"UNINITITALIZED",
	"INITIALIZED",
	"WAIT_PEER_ACTIVE",
	"ACTIVE",
	"LOW_POWER" };

static int link_msg_build(struct sk_buff *skb, int message, char cfg)
{
	char const sync[]     = { 0x01, 0x7e },
		   sync_rsp[] = { 0x02, 0x7d };
	char       conf[]     = { 0x03, 0xfc, cfg };
	char const conf_rsp[] = { 0x04, 0x7b };

	if (skb == NULL)
		return -EINVAL;

	if (message == BCSP_LINK_MSG_SYNC)
		skb_put_data(skb, sync, sizeof(sync));
	else if (message == BCSP_LINK_MSG_SYNC_RSP)
		skb_put_data(skb, sync_rsp, sizeof(sync_rsp));
	else if (message == BCSP_LINK_MSG_CONF)
		skb_put_data(skb, conf, sizeof(conf));
	else if (message == BCSP_LINK_MSG_CONF_RSP)
		skb_put_data(skb, conf_rsp, sizeof(conf_rsp));
	else
		return -EINVAL;
	return 0;
}

static int link_sleep_msg_build(struct sk_buff *skb, int message)
{
	char const wakeup[]   = { 0x05, 0xfa },
		   woken[]    = { 0x06, 0xf9 },
		   sleep[]    = { 0x07, 0x78 };

	if (skb == NULL)
		return -EINVAL;

	if (message == BCSP_LINK_MSG_WAKEUP)
		skb_put_data(skb, wakeup, sizeof(wakeup));
	else if (message == BCSP_LINK_MSG_WOKEN)
		skb_put_data(skb, woken, sizeof(woken));
	else if (message == BCSP_LINK_MSG_SLEEP)
		skb_put_data(skb, sleep, sizeof(sleep));
	else
		return -EINVAL;
	return 0;
}

static struct sk_buff *bcsp_link_msg_build(struct hci_bcsp *bcsp, int message)
{
	struct sk_buff *skb;
	int ret = 0;

	if (bcsp == NULL)
		return NULL;

	skb = alloc_hci_pkt_skb(0);
	if (skb == NULL)
		return NULL;
	SET_HCI_PKT_TYPE(skb, HCI_BCSP_TYPE_LINK_CONTROL);

	/* sleep related message as frequent, so let's check first */
	ret = link_sleep_msg_build(skb, message);
	if (ret == 0)
		return skb;

	ret = link_msg_build(skb, message, bcsp->cfg);
	if (ret == 0)
		return skb;

	kfree_skb(skb);
	return NULL;
}

static inline unsigned char *get_link_packet(struct sk_buff *skb)
{
	if (skb) {
		if (GET_HCI_PKT_TR_TYPE(skb) == HCI_TRANS_BCSP)
			return skb->data + BCSP_HEADER_SIZE;
		return skb->data;
	}
	return NULL;
}

static inline unsigned char get_link_packet_len(struct sk_buff *skb)
{
	if (skb) {
		if (GET_HCI_PKT_TR_TYPE(skb) == HCI_TRANS_BCSP)
			return skb->len - BCSP_HEADER_SIZE;
		return skb->len;
	}
	return 0;
}

static inline bool is_sig_link_msg(struct sk_buff *skb)
{
	unsigned char *p = get_link_packet(skb);

	return p != NULL ? p[0] < BCSP_LINK_MSG_MAX : false;
}

static unsigned int bcsp_link_get_msg(struct sk_buff *skb)
{
	const unsigned int sync = 0x017e, sync_rsp = 0x027d;
	const unsigned int conf = 0x03fc, conf_rsp = 0x047b;
	const unsigned int wakeup = 0x05fa, woken = 0x06f9, sleep = 0x0778;
	unsigned char *pkt = get_link_packet(skb);
	unsigned int msg;

	if (pkt == NULL || get_link_packet_len(skb) < 2)
		return BCSP_LINK_MSG_UNKNOWN;

	msg = (*(pkt) << 8) | *(pkt+1);
	if (msg == wakeup)
		return BCSP_LINK_MSG_WAKEUP;
	else if (msg == woken)
		return BCSP_LINK_MSG_WOKEN;
	else if (msg == sleep)
		return BCSP_LINK_MSG_SLEEP;
	else if (msg == sync)
		return BCSP_LINK_MSG_SYNC;
	else if (msg == sync_rsp)
		return BCSP_LINK_MSG_SYNC_RSP;
	else if (msg == conf)
		return BCSP_LINK_MSG_CONF;
	else if (msg == conf_rsp)
		return BCSP_LINK_MSG_CONF_RSP;
	return BCSP_LINK_MSG_UNKNOWN;
}

static inline unsigned char get_link_config(struct sk_buff *skb)
{
	const int cfg_idx = 2;

	if (bcsp_link_get_msg(skb) == BCSP_LINK_MSG_CONF_RSP)
		if (get_link_packet_len(skb) > cfg_idx)
			return get_link_packet(skb)[cfg_idx];
	return 0;
}

static void bcsp_link_peer_state_assume(struct hci_bcsp *bcsp, int msg)
{
	/* This assumes the controller state by the RESPONSE messages that
	 * host will send next. The state is set from here is expected state.
	 * This helps determine the time to send the first bcsp packet. The
	 * controller may not be ACTIVE state yet if host send bcsp packet
	 * immediately after receiving CONF_RSP.
	 */
	if (msg == BCSP_LINK_MSG_SYNC_RSP)
		bcsp->peer_state = HCI_BCSP_LINK_INITIALIZED;
	else if (msg == BCSP_LINK_MSG_CONF_RSP)
		bcsp->peer_state = HCI_BCSP_LINK_ACTIVE;
	else if (msg == BCSP_LINK_MSG_UNKNOWN)
		bcsp->peer_state = HCI_BCSP_LINK_UNINITITALIZED;
}

static inline bool is_bcsp_link_activated(struct hci_bcsp *bcsp)
{
	return (bcsp->state >= HCI_BCSP_LINK_ACTIVE &&
		bcsp->peer_state >= HCI_BCSP_LINK_ACTIVE);
}

static inline void bcsp_restore_credit(struct hci_bcsp *bcsp)
{
	TR_DBG("Restored credits to %d from %u\n", bcsp->window, bcsp->credit);
	bcsp->credit = bcsp->window;
}

static void bcsp_link_activated(struct hci_bcsp *bcsp)
{
	if (bcsp->peer_cfg != 0) {
		bcsp->window   = bcsp->peer_cfg & 0x7;
		bcsp->sw_fctrl = (bcsp->peer_cfg >> 3) & 0x1;
		bcsp->use_crc  = (bcsp->peer_cfg >> 4) & 0x1;
	} else {
		bcsp->window   = 4;
		bcsp->sw_fctrl = 0;
		bcsp->use_crc  = true;
	}
	bcsp->seq = 0;
	bcsp->ack = 0;

	TR_DBG("HCI bcsp link is activated. reset seq, ack. "
		"window=%u, use_crc=%d, sw_fctrl=%d\n",
		bcsp->window, bcsp->use_crc, bcsp->sw_fctrl);

	bcsp_restore_credit(bcsp);
}

static int bcsp_link_establishment(struct hci_bcsp *bcsp, struct sk_buff *rsp)
{
	struct sk_buff *skb = NULL;
	int rmsg = bcsp_link_get_msg(rsp);
	int msg = BCSP_LINK_MSG_UNKNOWN;

	if (bcsp == NULL)
		return -EINVAL;

	switch (bcsp->state) {
	case HCI_BCSP_LINK_UNINITITALIZED:
		switch (rmsg) {
		case BCSP_LINK_MSG_UNKNOWN:
			msg = BCSP_LINK_MSG_SYNC;
			bcsp_set_resend_timeout(bcsp);
			break;
		case BCSP_LINK_MSG_SYNC:
			msg = BCSP_LINK_MSG_SYNC_RSP;
			break;
		case BCSP_LINK_MSG_SYNC_RSP:
			bcsp->state = HCI_BCSP_LINK_INITIALIZED;
			msg = BCSP_LINK_MSG_CONF;
			bcsp_set_resend_timeout(bcsp);
			break;
		default:
			TR_WARNING("UNINITIALIZED state: Invalid message(%s)\n",
				message_to_str[rmsg]);
			msg = BCSP_LINK_MSG_SYNC;
			bcsp_set_resend_timeout(bcsp);
			break;
		}
		break;

	case HCI_BCSP_LINK_INITIALIZED:
		switch (rmsg) {
		case BCSP_LINK_MSG_UNKNOWN:    /* Time out */
			msg = BCSP_LINK_MSG_CONF;
			bcsp_set_resend_timeout(bcsp);
			break;
		case BCSP_LINK_MSG_SYNC:
			msg = BCSP_LINK_MSG_SYNC_RSP;
			break;
		case BCSP_LINK_MSG_CONF:
			msg = BCSP_LINK_MSG_CONF_RSP;
			break;
		case BCSP_LINK_MSG_CONF_RSP:
			del_timer(&bcsp->resend_timer);
			bcsp->peer_cfg = get_link_config(rsp);

			if (bcsp->peer_state == HCI_BCSP_LINK_ACTIVE)
				bcsp->state = HCI_BCSP_LINK_ACTIVE;
			else
				/* Controller may not ready to receive */
				bcsp->state = HCI_BCSP_LINK_WAIT_PEER_ACTIVE;
			break;
		default:
			TR_ERR("INITIALIZED state: Invalid message(%s)\n",
				message_to_str[rmsg]);
			return -EINVAL;
		}
		break;

	case HCI_BCSP_LINK_WAIT_PEER_ACTIVE:
		if (rmsg == BCSP_LINK_MSG_CONF)
			bcsp->state = HCI_BCSP_LINK_ACTIVE;
		/* fall through */
		FALL_THROUGH();
	case HCI_BCSP_LINK_ACTIVE:
	case HCI_BCSP_LINK_LOW_POWER:
		switch (rmsg) {
		case BCSP_LINK_MSG_CONF:
			msg = BCSP_LINK_MSG_CONF_RSP;
			break;
		case BCSP_LINK_MSG_SYNC:
			TR_ERR("BCSP_LINK_MSG_SYNC is received.\n");
			slsi_bt_err(SLSI_BT_ERR_BCSP_FAIL);
			return -EINVAL;
		default:
			TR_ERR("ACTIVE state: Invalid message(%s)\n",
				message_to_str[rmsg]);
			return -EINVAL;
		}
		break;
	}

	if (msg != BCSP_LINK_MSG_UNKNOWN) {
		skb = bcsp_link_msg_build(bcsp, msg);
		if (skb == NULL)
			return 0;
		SET_HCI_PKT_TYPE(skb, HCI_BCSP_TYPE_LINK_CONTROL);
		skb_queue_tail(&bcsp->unrel_q, skb);
		bcsp_link_peer_state_assume(bcsp, msg);
	}

	TR_INFO("%s is received, send %s (link state host=%s controller=%s)\n",
		message_to_str[rmsg], message_to_str[msg],
		state_to_str[bcsp->state], state_to_str[bcsp->peer_state]);

	if (bcsp->state == HCI_BCSP_LINK_ACTIVE)
		bcsp_link_activated(bcsp);

	return 0;
}

static int bcsp_link_low_power(struct hci_bcsp *bcsp, struct sk_buff *rsp)
{
	struct sk_buff *skb = NULL;
	int msg = bcsp_link_get_msg(rsp);
	int ret = 0;

	if (bcsp == NULL)
		return -EINVAL;

	switch (msg) {
	case BCSP_LINK_MSG_WAKEUP:
		skb = bcsp_link_msg_build(bcsp, BCSP_LINK_MSG_WOKEN);
		if (skb == NULL) {
			ret = -ENOMEM;
			break;
		}
		skb_queue_head(&bcsp->unrel_q, skb);
		/* fall through: WAKEUP reception means bt is already awake */
		FALL_THROUGH();
	case BCSP_LINK_MSG_WOKEN:
		del_timer(&bcsp->wakeup_timer);
		if (bcsp->peer_state == HCI_BCSP_LINK_LOW_POWER) {
			bcsp->peer_state = HCI_BCSP_LINK_ACTIVE;
			if (bcsp->wakeup_timeout_cnt > 0) {
				BT_DBG("woken! wakeup retried %d times\n",
					bcsp->wakeup_timeout_cnt);
				bcsp->wakeup_timeout_cnt = 0;
			}

			if (bcsp_cross_scenario_detected(bcsp)) {
				BT_DBG("Cross scenario detected. "
				       "The last packet have been lost.\n");
				bcsp_reload_unacked_packets(bcsp);
			}
		}
		break;
	case BCSP_LINK_MSG_SLEEP:
		bcsp->peer_state = HCI_BCSP_LINK_LOW_POWER;
		if (bcsp_cross_scenario_detected(bcsp)) {
			BT_DBG("Cross scenario detected. "
			       "BT should be woken in wakeup timeout.\n");
			bcsp_set_wakeup_timeout(bcsp, HCI_BCSP_RESEND_WAKEUP_AFTER_CROSS_MS);
		}
		break;
	default:
		/* Whatever comes, the controller is awake */
		bcsp->peer_state = HCI_BCSP_LINK_ACTIVE;
		ret = -EINVAL;
	}
	TR_DBG("%s is received, controller is in %s state\n",
		message_to_str[msg], state_to_str[bcsp->peer_state]);
	return ret;
}

static void bcsp_link_wakeup(struct hci_bcsp *bcsp)
{
	struct sk_buff *skb;

	if (bcsp->peer_state == HCI_BCSP_LINK_LOW_POWER) {
		if (timer_pending(&bcsp->wakeup_timer)) {
			TR_DBG("wakeup timer is not updated. It's pending\n");
			return;
		}

		skb = bcsp_link_msg_build(bcsp, BCSP_LINK_MSG_WAKEUP);
		if (skb == NULL)
			return;
		skb_queue_head(&bcsp->unrel_q, skb);
		bcsp_set_wakeup_timeout(bcsp, HCI_BCSP_RESEND_WAKEUP_MS);
	}
}

static int bcsp_link_control_handle(struct hci_bcsp *bcsp, struct sk_buff *rsp)
{
	if (bcsp == NULL)
		return -EINVAL;

	switch (bcsp->peer_state) {
	case HCI_BCSP_LINK_ACTIVE:
	case HCI_BCSP_LINK_LOW_POWER:
		if (bcsp_link_low_power(bcsp, rsp) == 0)
			return 0;
		/* fall through */
		FALL_THROUGH();
	case HCI_BCSP_LINK_UNINITITALIZED:
	case HCI_BCSP_LINK_INITIALIZED:
	case HCI_BCSP_LINK_WAIT_PEER_ACTIVE:
		return bcsp_link_establishment(bcsp, rsp);
	}
	TR_WARNING("Link control packet is received in unexpected state\n");
	return -EINVAL;
}

static int bcsp_vspmp_handle(struct hci_bcsp *bcsp, struct sk_buff *skb)
{
	/* The VSPMP should be handled at the upper layers, but only FASTPATH
	 * channel is handled here due to its H5 dependency */
	const char *p = skb->data + BCSP_HEADER_SIZE;
	const int plen = skb->len - BCSP_HEADER_SIZE - (bcsp->use_crc ? 2 : 0);

	if (plen < 0)
		return -EINVAL;

	switch (*p) {
	case VSPMP_CHANNEL_FASTPATH:
		TR_DBG("Fastpath control packet\n");
		return fastpath_control_packet_handle(bcsp, p, plen);
	}
	return 0;
}

/******************************************************************************/

static int bcsp_skb_send(struct hci_bcsp *bcsp, struct sk_buff *skb)
{
	struct sk_buff *slip_skb = NULL;

	if (bcsp == NULL || skb == NULL)
		return -EINVAL;

	bcsp_skb_update_packet(bcsp, skb);
	bcsp->ack_req = false;
	TR_DBG("bytes of BCSP packet=%u\n", skb->len);
	BTTR_TRACE_HEX(BTTR_TAG_BCSP_TX, skb);

	/* Slip Packing */
	slip_skb = slip_encode(skb->data, skb->len, bcsp->sw_fctrl);
	if (!slip_skb) {
		TR_ERR("error to allocate skb\n");
		slsi_bt_err(SLSI_BT_ERR_NOMEM);
		return -ENOMEM;
	}

	/*************************
	 * Send to next transport
	 *************************/
	TR_DBG("bytes of SLIP packet: %u\n", slip_skb->len);
	BTTR_TRACE_HEX(BTTR_TAG_SLIP_TX, slip_skb);
	return hci_trans_send_skb(bcsp->htr, slip_skb);
}

static inline bool sending_standby(struct hci_bcsp *bcsp)
{
	if (bcsp->peer_state == HCI_BCSP_LINK_LOW_POWER &&
	    bcsp_cross_scenario_detected(bcsp))
		return true;

	if (!skb_queue_empty(&bcsp->unrel_q))
		return true;

	return (is_bcsp_link_activated(bcsp) &&
		!skb_queue_empty(&bcsp->rel_q) &&
		bcsp->credit > 0);
}

static void txwork_unreliable_channel(struct hci_bcsp *bcsp)
{
	struct sk_buff *skb = skb_dequeue(&bcsp->unrel_q);

	if (skb) {
		TR_DBG("%d bytes\n", skb->len);
		bcsp_skb_send(bcsp, skb);
		kfree_skb(skb);
	}
}

static void txwork_reliable_channel(struct hci_bcsp *bcsp)
{
	struct sk_buff *skb = NULL;

	if (bcsp->credit == 0)
		return;

	skb = skb_dequeue(&bcsp->rel_q);
	if (skb) {
		TR_DBG("%u bytes\n", skb->len);
		bcsp_skb_send(bcsp, skb);

		bcsp->credit--;
		skb_queue_tail(&bcsp->sent_q, skb);
		TR_DBG("reset resend_timer to %lu\n", HCI_BCSP_RESEND_TIMEOUT);
		bcsp_set_resend_timeout(bcsp);
	}
}

static void txwork_acknowledgement(struct hci_bcsp *bcsp)
{
	struct sk_buff *skb = alloc_hci_pkt_skb(0);

	if (skb) {
		TR_DBG("send ack: %u\n", bcsp->ack);
		SET_HCI_PKT_TYPE(skb, HCI_BCSP_TYPE_ACK);
		bcsp_skb_send(bcsp, skb);
		kfree_skb(skb);
	}
}

static void _bcsp_txwork(struct hci_bcsp *bcsp)
{
	while (sending_standby(bcsp)) {

		/* 1. wake up */
		if (bcsp->peer_state == HCI_BCSP_LINK_LOW_POWER) {
			bcsp_link_wakeup(bcsp);
			txwork_unreliable_channel(bcsp);
			/* It needs at least a one character gap between the
			 * sending of each Wakeup message by SIG core 5.3. but,
			 * this bcsp stack does not control uart device
			 * directly so it sets a guard time to wait after sent
			 * this pacekt.
			 */
			udelay(1000);
			return;
		}

		/* 2. unreliable packet */
		txwork_unreliable_channel(bcsp);

		/* 3. reliable packet */
		if (is_bcsp_link_activated(bcsp))
			txwork_reliable_channel(bcsp);
	}

	if (bcsp->ack_req)
		txwork_acknowledgement(bcsp);
}

static void bcsp_txwork_func(struct work_struct *work)
{
	struct hci_bcsp *bcsp = container_of(work, struct hci_bcsp, work_tx);

	if (!test_bit(HCI_BCSP_OPENED, &bcsp->flags)) {
		TR_ERR("txwork is called after closing bcsp htr.\n");
		return;
	}

	if (slsi_bt_err_status()) {
		TR_WARNING("bt is in error status.\n");
		del_timer(&bcsp->resend_timer);
		del_timer(&bcsp->wakeup_timer);
		return;
	}

	mutex_lock(&bcsp->lock);
	TR_DBG("credits=%u, unrel_q#=%u, rel_q#=%u\n", bcsp->credit,
		skb_queue_len(&bcsp->unrel_q), skb_queue_len(&bcsp->rel_q));

	wake_lock(&bcsp->tx_wake_lock);
	do {
		clear_bit(HCI_BCSP_TX_WAKEUP, &bcsp->flags);
		_bcsp_txwork(bcsp);
		clear_bit(HCI_BCSP_SENDING, &bcsp->flags);
	} while (test_bit(HCI_BCSP_TX_WAKEUP, &bcsp->flags));
	wake_unlock(&bcsp->tx_wake_lock);

	mutex_unlock(&bcsp->lock);
	TR_DBG("exit\n");
}

static void bcsp_write_wakeup(struct hci_bcsp *bcsp)
{
	if (!sending_standby(bcsp) && !bcsp->ack_req)
		return;

	set_bit(HCI_BCSP_TX_WAKEUP, &bcsp->flags);
	if (test_and_set_bit(HCI_BCSP_SENDING, &bcsp->flags))
		return;

	if (test_bit(HCI_BCSP_OPENED, &bcsp->flags))
		schedule_work(&bcsp->work_tx);
}

static void bcsp_timeoutwork_func(struct work_struct *work)
{
	struct hci_bcsp *bcsp = container_of(work, struct hci_bcsp,
		work_timeout);

	mutex_lock(&bcsp->lock);
	TR_DBG("status=%s, resend count=%d, retry_limit=%d\n",
		state_to_str[bcsp->state], bcsp->resend_cnt,
		HCI_BCSP_RESEND_LIMIT);

	if (bcsp->state < HCI_BCSP_LINK_ACTIVE) {
		TR_DBG("link establishment timeout.\n");
		bcsp_link_establishment(bcsp, NULL);
	} else if (++bcsp->resend_cnt > HCI_BCSP_RESEND_LIMIT) {
		TR_ERR("The retry limit has been reached\n");
		slsi_bt_err(SLSI_BT_ERR_BCSP_RESEND_LIMIT);
	} else
		bcsp_reload_unacked_packets(bcsp);

	bcsp_write_wakeup(bcsp);
	mutex_unlock(&bcsp->lock);
}

static void bcsp_send_timeout(struct timer_list *t)
{
	struct hci_bcsp *bcsp = from_timer(bcsp, t, resend_timer);

	if (test_bit(HCI_BCSP_OPENED, &bcsp->flags)) {
		if (bcsp->state == HCI_BCSP_LINK_UNINITITALIZED)
			TR_DBG("waiting for starting the controller\n");
		else
			TR_WARNING("timeout: resend_timer is expired\n");
		schedule_work(&bcsp->work_timeout);
	}
}

static void bcsp_wakeup_timeout(struct timer_list *t)
{
	struct hci_bcsp *bcsp = from_timer(bcsp, t, wakeup_timer);
	const int warning_cnt = 5;
	const int max_cnt = 20;

	if (!test_bit(HCI_BCSP_OPENED, &bcsp->flags))
		return;

	if (bcsp->peer_state != HCI_BCSP_LINK_LOW_POWER || !bcsp->htr)
		return;

	bcsp->wakeup_timeout_cnt++;
	if (bcsp->wakeup_timeout_cnt == 1) {
		TR_DBG("wakeup retry, attempt %d\n", bcsp->wakeup_timeout_cnt);

	} else if (bcsp->wakeup_timeout_cnt >= max_cnt) {
		TR_ERR("The wakeup retry limit has been reached (%d)\n",
			bcsp->wakeup_timeout_cnt);
		return;

	} else if (bcsp->wakeup_timeout_cnt % warning_cnt == 0) {
		TR_WARNING("wakeup retry, attempt %d\n",
			bcsp->wakeup_timeout_cnt);
	}

	/* wakeup message will be resend in txwork. if txwork is still
	 * working, wakeup timer start again to prevent missing retry.
	 */
	if (test_bit(HCI_BCSP_SENDING, &bcsp->flags))
		bcsp_set_wakeup_timeout(bcsp, HCI_BCSP_RESEND_WAKEUP_MS);
	else
		bcsp_write_wakeup(bcsp);
}

static void bcsp_ack_handle(struct hci_bcsp *bcsp, unsigned char rack)
{
	struct sk_buff *skb;
	unsigned char ack_cnt = ((rack + 8) - bcsp->rack) & 0x7;

	if (bcsp->credit + ack_cnt == 0)
		TR_DBG("No more credit. wait for next ack\n");
	else if (bcsp->credit + ack_cnt > bcsp->window)
		TR_WARNING("It's overrun ack.\n");

	/* Remove acked packets */
	if (bcsp->seq == rack) {
		skb_queue_purge(&bcsp->sent_q);
		bcsp_restore_credit(bcsp);
		TR_DBG("del_timer\n");
		del_timer(&bcsp->resend_timer);

		slsi_bt_fastpath_acknowledgement_all();
	} else {
		unsigned char key = (rack - ack_cnt) & 0x7;
		unsigned char _cnt = ack_cnt;

		TR_DBG("ack_cnt=%d, wait for %d of acks\n",
			ack_cnt, skb_queue_len(&bcsp->sent_q));
		while (_cnt-- && !skb_queue_empty(&bcsp->sent_q)) {
			skb = skb_dequeue(&bcsp->sent_q);
			bcsp->credit++;

			slsi_bt_fastpath_acknowledgement(key);
			key = (key + 1) & 0x7;
			kfree_skb(skb);
		}
	}

	bcsp->rack = rack;
	bcsp->resend_cnt = 0;

	TR_DBG("host seq=%u, rack=%u ack_cnt=%u, pending=%u, credits=%u\n",
		bcsp->seq, bcsp->rack, ack_cnt, skb_queue_len(&bcsp->sent_q),
		bcsp->credit);
}

static int bcsp_skb_recv_handle(struct hci_bcsp *bcsp, struct sk_buff *skb)
{
	unsigned char rseq, rack, use_crc, reliable, type;
	unsigned char check_sum;
	size_t len;
	int err = 0;

	if (skb->len < BCSP_HEADER_SIZE) {
		TR_ERR("Small bcsp packet\n");
		return -EINVAL;
	}

	rseq      = skb->data[0] & 0x7;
	rack      = (skb->data[0] >> 3) & 0x7;
	use_crc   = (skb->data[0] >> 6) & 0x1;
	reliable  = (skb->data[0] >> 7) & 0x1;
	type      = skb->data[1] & 0xF;
	len       = ((skb->data[1] >> 4) & 0xF) | (skb->data[2] << 4);
	check_sum = ~(skb->data[0] + skb->data[1] + skb->data[2]) & 0xff;
	if (check_sum != skb->data[3]) {
		TR_ERR("header checksum error: expected %u, but has %u\n",
			check_sum, skb->data[3]);
		err = -EINVAL;
		goto error;
	}
	TR_DBG("link state is %s, expected ack:%u "
		"rx seq:%u, ack:%u, crc:%u, rel:%u, type:%u, payload len:%zu\n",
		state_to_str[bcsp->state], bcsp->ack,
		rseq, rack, use_crc, reliable, type, len);

	if (skb->len != len + (use_crc ? 6 : 4)) {
		TR_ERR("SLIP payload length error. it has %u\n", skb->len);
		err = -EINVAL;
		goto error;
	}

	if (use_crc) {
		if (bcsp_skb_check_crc(skb) == false) {
			TR_ERR("data integrity check error\n");
			err = -EINVAL;
			goto error;
		}
		skb_trim(skb, skb->len - 2);
	}

	/* Handle reliable packet */
	if (reliable) {
		if (bcsp->ack == rseq) {
			bcsp->ack = (bcsp->ack + 1) & 0x7;
		} else {
			bcsp->ack_mismatch++;
			TR_ERR("out of sequence packet error: "
				"expected ack=%u, received ack=%u\n",
				bcsp->ack, rseq);
			err = -EINVAL;
			goto error;
		}
	}

	if (is_bcsp_link_activated(bcsp))
		bcsp_ack_handle(bcsp, rack);
	SET_HCI_PKT_TYPE(skb, type);
error:
	if (reliable)
		bcsp->ack_req = true;
	return err;
}

static void bcsp_skb_recv(struct hci_bcsp *bcsp, struct sk_buff *skb)
{
	if (bcsp == NULL || skb == NULL)
		return;

	switch (GET_HCI_PKT_TYPE(skb)) {
	case HCI_BCSP_TYPE_ACK:
		TR_DBG("Acknowledgement packet\n");
		break;
	case HCI_BCSP_TYPE_LINK_CONTROL:
		TR_DBG("Link control packet\n");
		bcsp_link_control_handle(bcsp, skb);
		break;
	case HCI_BCSP_TYPE_VENDOR:
		if (bcsp_vspmp_handle(bcsp, skb))
			return;
		FALL_THROUGH();
	default:
		if (!is_bcsp_link_activated(bcsp)) {
			TR_WARNING("Packet is received in inactive state\n");
			return;
		}
		TR_DBG("HCI packet\n");
		/* Remove bcsp header */
		skb_pull(skb, BCSP_HEADER_SIZE);
		SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_HCI);
	}
}

static void bcsp_reload_unacked_packets(struct hci_bcsp *bcsp)
{
	struct sk_buff *skb = NULL;

	TR_DBG("reload %u packet(s) to rel_q\n", skb_queue_len(&bcsp->sent_q));
	while ((skb = skb_dequeue_tail(&bcsp->sent_q)) != NULL) {
		bcsp_update_fastpath_infor(skb, FASTPATH_INVALID_KEY);
		skb_queue_head(&bcsp->rel_q, skb);
		bcsp->seq = (bcsp->seq - 1) & 0x7;
	}
	bcsp_restore_credit(bcsp);
}

static int hci_bcsp_skb_send(struct hci_trans *htr, struct sk_buff *skb)
{
	struct hci_bcsp *bcsp = get_hci_bcsp(htr);

	if (htr == NULL || skb == NULL || bcsp == NULL)
		return -EINVAL;

	mutex_lock(&bcsp->lock);
	TR_DBG("hci packet type:%d, len:%u\n", GET_HCI_PKT_TYPE(skb), skb->len);

	if (slsi_bt_fastpath_available(skb)) {
		skb = change_to_fastpath(skb);
		if (skb == NULL)
			return -ENOMEM;
	}

	if (bcsp_check_reliable(GET_HCI_PKT_TYPE(skb)))
		skb_queue_tail(&bcsp->rel_q, skb);
	else
		skb_queue_tail(&bcsp->unrel_q, skb);

	bcsp_write_wakeup(bcsp);
	mutex_unlock(&bcsp->lock);
	return 0;
}

static int hci_bcsp_skb_recv(struct hci_trans *htr, struct sk_buff *slip_skb)
{
	struct hci_bcsp *bcsp = get_hci_bcsp(htr);
	struct sk_buff *skb;
	int ret;

	if (slip_skb == NULL)
		return -EINVAL;
	if (htr == NULL || bcsp == NULL) {
		kfree_skb(slip_skb);
		return -EINVAL;
	}
	if (!slip_check_complete(slip_skb)) {
		kfree_skb(slip_skb);
		TR_ERR("It is not a SLIP packet\n");
		return -EINVAL;
	}

	BTTR_TRACE_HEX(BTTR_TAG_SLIP_RX, slip_skb);
	skb = slip_decode(slip_skb->data, slip_skb->len);
	ret = slip_skb->len;
	kfree_skb(slip_skb);
	if (skb == NULL) {
		TR_ERR("SLIP decoding failed\n");
		return -ENOMEM;;
	}

	if (!test_bit(HCI_BCSP_OPENED, &bcsp->flags)) {
		kfree_skb(skb);
		return -EIO;
	}

	// rx bcsp
	mutex_lock(&bcsp->lock);
	BTTR_TRACE_HEX(BTTR_TAG_BCSP_RX, skb);
	if (bcsp_skb_recv_handle(bcsp, skb) == 0)
		bcsp_skb_recv(bcsp, skb);
	else {
		BT_WARNING("bcsp has failed. "
			   "The packet is discarded.\n");
		ret = -EINVAL;
	}
	bcsp_write_wakeup(bcsp);
	mutex_unlock(&bcsp->lock);

	if (ret > 0 && GET_HCI_PKT_TR_TYPE(skb) == HCI_TRANS_HCI)
		return hci_trans_recv_skb(htr, skb);

	kfree_skb(skb);
	return ret;
}

static ssize_t hci_bcsp_recv(struct hci_trans *htr, const char *data,
		size_t count, unsigned int flags)
{
	struct hci_bcsp *bcsp = get_hci_bcsp(htr);
	ssize_t ret = 0;

	if (htr == NULL || bcsp == NULL || data == NULL)
		return 0;

	if (!test_bit(HCI_BCSP_OPENED, &bcsp->flags)) {
		return -EIO;
	}

	mutex_lock(&bcsp->lock);
	ret = slip_collect_pkt(&bcsp->collect_skb, data, count);
	if (ret < 0) {
		BT_WARNING("cannot collect SLIP packet: "
			   "drop all bytes\n");
		ret = count;
		goto out;
	} else if (bcsp->collect_skb == NULL) {
		BT_WARNING("cannot collect SLIP packet: "
			   "drop %zd bytes\n", ret);
		goto out;
	}

	if (GET_HCI_PKT_TR_TYPE(bcsp->collect_skb) == HCI_TRANS_BCSP) {
		// Collecting is completed
		struct sk_buff *skb = bcsp->collect_skb;

		bcsp->collect_skb = NULL;
		mutex_unlock(&bcsp->lock);

		hci_bcsp_skb_recv(htr, skb);
		return ret;
	}
out:
	mutex_unlock(&bcsp->lock);
	return ret;
}

static int hci_bcsp_proc_show(struct hci_trans *htr, struct seq_file *m)
{
	struct hci_bcsp *bcsp = get_hci_bcsp(htr);

	mutex_lock(&bcsp->lock);
	seq_printf(m, "\n  %s:\n", hci_trans_get_name(htr));
	seq_printf(m, "    bcsp locking state     = %u\n",
		mutex_is_locked(&bcsp->lock));
	seq_printf(m, "    TX Work running state  = %u\n",
		test_bit(HCI_BCSP_SENDING, &bcsp->flags));
	seq_puts(m, "\n");
	seq_printf(m, "    Reliable queue length  = %u\n",
		skb_queue_len(&bcsp->rel_q));
	seq_printf(m, "    Unreliable queue length= %u\n",
		skb_queue_len(&bcsp->unrel_q));
	seq_printf(m, "    Wait for ack packets   = %u\n",
		skb_queue_len(&bcsp->sent_q));
	seq_puts(m, "\n");
	seq_printf(m, "    Re-send timeout        = %d ms\n", 1000/4);
	seq_printf(m, "    Re-send count (timeout)= %d\n", bcsp->resend_cnt);
	seq_printf(m, "    Ack mismatch count     = %d\n", bcsp->ack_mismatch);
	seq_puts(m, "\n");
	seq_printf(m, "    Link state (host)      = %s\n",
		state_to_str[bcsp->state]);
	seq_printf(m, "    Link state (controller)= %s\n",
		state_to_str[bcsp->peer_state]);
	seq_puts(m, "\n");
	seq_printf(m, "    BCSP Configure         = %d\n", bcsp->cfg);
	seq_printf(m, "    BCSP Peer Configure    = %d\n", bcsp->peer_cfg);
	seq_puts(m, "\n");
	seq_printf(m, "    BCSP Sequence number   = %u\n", bcsp->seq);
	seq_printf(m, "    BCSP Acknowledgement   = %u\n", bcsp->ack);
	seq_printf(m, "    BCSP Last received ack = %u\n", bcsp->rack);
	seq_printf(m, "    BCSP Use CRC           = %d\n", bcsp->use_crc);
	seq_printf(m, "    BCSP Current credits   = %u\n", bcsp->credit);
	seq_printf(m, "    BCSP Ack requests      = %d\n", bcsp->ack_req);
	mutex_unlock(&bcsp->lock);
	return slsi_bt_fastpath_proc_show(m);
}

int hci_bcsp_open(struct hci_trans *htr)
{
	struct hci_bcsp *bcsp = &slsi_bt_bcsp;

	if (htr == NULL)
		return -EINVAL;

	/* Setup HCI transport for bcsp */
	htr->send_skb = hci_bcsp_skb_send;
	htr->recv_skb = hci_bcsp_skb_recv;
	htr->recv = hci_bcsp_recv;
	htr->proc_show = hci_bcsp_proc_show;
	htr->deinit = hci_bcsp_close;
	htr->tdata = bcsp;

	bcsp->htr = htr;
	bcsp->wakeup_timeout_cnt = 0;
	bcsp->seq = 0;
	bcsp->ack = 0;
	bcsp->rack = 0;
	bcsp->ack_req = 0;

	set_bit(HCI_BCSP_OPENED, &bcsp->flags);

	/* Link Establishment */
	bcsp_link_peer_state_assume(bcsp, HCI_BCSP_LINK_UNINITITALIZED);
	bcsp_link_establishment(bcsp, NULL);
	fastpath_enable(bcsp, true);
	bcsp_write_wakeup(bcsp);

	BT_INFO("done\n");
	return 0;
}

void hci_bcsp_close(struct hci_trans *htr)
{
	struct hci_bcsp *bcsp = &slsi_bt_bcsp;

	TR_INFO("close\n");

	if (!test_bit(HCI_BCSP_OPENED, &bcsp->flags))
		return;

	clear_bit(HCI_BCSP_OPENED, &bcsp->flags);

	if (slsi_bt_fastpath_supported())
		slsi_bt_fastpath_activate(false);

	del_timer(&bcsp->resend_timer);
	del_timer(&bcsp->wakeup_timer);

	clear_bit(HCI_BCSP_TX_WAKEUP, &bcsp->flags);
	cancel_work_sync(&bcsp->work_tx);
	cancel_work_sync(&bcsp->work_timeout);

	mutex_lock(&bcsp->lock);
	bcsp->htr->tdata = NULL;
	bcsp->htr = NULL;

	skb_queue_purge(&bcsp->rel_q);
	skb_queue_purge(&bcsp->sent_q);
	skb_queue_purge(&bcsp->unrel_q);
	bcsp->ack_req = 0;

	if (bcsp->collect_skb) {
		kfree_skb(bcsp->collect_skb);
		bcsp->collect_skb = NULL;
	}

	bcsp->state = HCI_BCSP_LINK_UNINITITALIZED;
	bcsp->peer_state = HCI_BCSP_LINK_UNINITITALIZED;
	bcsp->flags = 0;

	mutex_unlock(&bcsp->lock);
}

int hci_bcsp_init(void)
{
	struct hci_bcsp *bcsp = &slsi_bt_bcsp;

	bcsp->use_crc = false;
	bcsp->cfg = HCI_BCSP_WINDOWSIZE & 0x7;
	bcsp->cfg |= (HCI_BCSP_SW_FLOW_CTR & 0x1) << 3;
	bcsp->cfg |= (HCI_BCSP_DATA_INT_TYPE & 0x1) << 4;
	bcsp->cfg |= (HCI_BCSP_VERSION & 0x3) << 5;

	wake_lock_init(NULL, &bcsp->tx_wake_lock.ws, "bt_tx_wake_lock");
	INIT_WORK(&bcsp->work_tx, bcsp_txwork_func);
	INIT_WORK(&bcsp->work_timeout, bcsp_timeoutwork_func);

	mutex_init(&bcsp->lock);

	skb_queue_head_init(&bcsp->rel_q);
	skb_queue_head_init(&bcsp->sent_q);
	skb_queue_head_init(&bcsp->unrel_q);

	timer_setup(&bcsp->resend_timer, bcsp_send_timeout, 0);
	timer_setup(&bcsp->wakeup_timer, bcsp_wakeup_timeout, 0);

	BT_INFO("Init completed\n");
	return 0;
}

void hci_bcsp_deinit(void)
{
	struct hci_bcsp *bcsp = &slsi_bt_bcsp;

	hci_bcsp_close(NULL);

	/* free resources */
	mutex_destroy(&bcsp->lock);
	wake_lock_destroy(&bcsp->tx_wake_lock);

	BT_INFO("Deinit.\n");
}
