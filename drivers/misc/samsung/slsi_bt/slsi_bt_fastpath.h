/******************************************************************************
 *                                                                            *
 * Copyright (c) 2024 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 ******************************************************************************/
#ifndef __SLSI_BT_FASTPATH__
#define __SLSI_BT_FASTPATH__

#include <linux/skbuff.h>

#ifndef VSPMP_CHANNEL_FASTPATH
#define VSPMP_CHANNEL_FASTPATH   (2)
#endif

#ifndef UNUSED
#define UNUSED(x)       ((void)(x))
#endif

enum fastpath_tags {
	FASTPATH_ENABLE_REQ_TAG = 0x01,
	FASTPATH_ENABLE_CFM_TAG,
	FASTPATH_TO_AIR_ACTIVATE_IND_TAG,
	FASTPATH_TO_AIR_ACTIVATE_RSP_TAG,
	FASTPATH_DATAGRAM_TAG,
};
#define FASTPATH_ENABLE_REQ_LEN              (1)
#define FASTPATH_TO_AIR_ACTIVATE_RSP_LEN     (0)
#define FASTPATH_DATAGRAM_LEN                (7)

#define FASTPATH_ENABLE_REQ_PKT_SIZE         (FASTPATH_ENABLE_REQ_LEN + 3)
#define FASTPATH_TO_AIR_ACTIVATE_RSP_PKT_SIZE \
		(FASTPATH_TO_AIR_ACTIVATE_RSP_LEN + 3)
#define FASTPATH_DATAGRAM_PKT_SIZE           (FASTPATH_DATAGRAM_LEN + 3)

#define FASTPATH_INVALID_KEY                 (-1)

/*
 * fastpath_datagrams are transmitted in both directions between the controller
 * and the host in an UART transport layer packet. This packet has a location
 * offset in the shared memory embedded which holds the actual data for HCI
 * packets to be transmitted between the controller and the host.
 */
struct fastpath_datagram {
	uint8_t  type;
	uint16_t length;
	uint32_t location;
};

#if IS_ENABLED(CONFIG_SLSI_BT_FASTPATH)
bool slsi_bt_fastpath_supported(void);
void slsi_bt_fastpath_activate(bool active);
bool slsi_bt_fastpath_available(struct sk_buff *skb);

struct sk_buff *slsi_bt_fastpath_read(struct fastpath_datagram *datagram);
int slsi_bt_fastpath_write(struct sk_buff *skb,
			   struct fastpath_datagram *datagram);
void slsi_bt_fastpath_update_key(const char *p, size_t len, int key);
inline void slsi_bt_fastpath_acknowledgement_all(void);
inline void slsi_bt_fastpath_acknowledgement(int key);
int slsi_bt_fastpath_proc_show(struct seq_file *m);

inline struct sk_buff *slsi_bt_fastpath_enable_req_packet(bool enable);
inline struct sk_buff *slsi_bt_fastpath_to_air_activate_rsp_packet(void);
inline struct sk_buff *slsi_bt_fastpath_datagram_packet(
					struct fastpath_datagram *datagram);

inline void slsi_bt_fastpath_datagram_to_bytes(char *dst,
					const struct fastpath_datagram *src);

inline void slsi_bt_fastpath_bytes_to_datagram(struct fastpath_datagram *dst,
					       const char *src);

#else /* CONFIG_SLSI_BT_FASTPATH */
#define slsi_bt_fastpath_supported()                  (false)
#define slsi_bt_fastpath_activate(active)
#define slsi_bt_fastpath_available(skb)               (false)

#define slsi_bt_fastpath_read(datagram)               (NULL)
#define slsi_bt_fastpath_write(skb, datagram)         (-EINVAL)

#define slsi_bt_fastpath_update_key(p, len, key)
#define slsi_bt_fastpath_acknowledgement_all()
#define slsi_bt_fastpath_acknowledgement(key)
#define slsi_bt_fastpath_proc_show(m)                 (0)

#define slsi_bt_fastpath_enable_req_packet(enable)    (NULL)
#define slsi_bt_fastpath_to_air_activate_rsp_packet() (NULL)
static inline struct sk_buff *slsi_bt_fastpath_datagram_packet(
					struct fastpath_datagram *datagram)
{
	UNUSED(datagram);
	return NULL;
}

#define slsi_bt_fastpath_datagram_to_bytes(dst, src)
#define slsi_bt_fastpath_bytes_to_datagram(dst, src) UNUSED(dst); UNUSED(src);

#endif /* CONFIG_SLSI_BT_FASTPATH */
#endif /* __SLSI_BT_FASTPATH__ */
