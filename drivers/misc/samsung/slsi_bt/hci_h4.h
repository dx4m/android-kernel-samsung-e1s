/******************************************************************************
 *                                                                            *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * Bluetooth HCI Uart Transport Layer & H4                                    *
 *                                                                            *
 ******************************************************************************/
#ifndef __HCI_H4_H__
#define __HCI_H4_H__
#include "hci_pkt.h"
#include "hci_trans.h"

#define HCI_H4_PKT_TYPE_SIZE          1

static inline struct sk_buff *alloc_hci_h4_pkt_skb(int size)
{
	struct sk_buff *skb = __alloc_hci_pkt_skb(size, HCI_H4_PKT_TYPE_SIZE);
	if (skb)
		SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_H4);
	else
		BT_WARNING("allocation failed\n");
	return skb;
}

#if IS_ENABLED(CONFIG_SLSI_BT_H4)
int hci_h4_init(struct hci_trans *htr, bool reverse);
void hci_h4_deinit(struct hci_trans *htr);
#else
#define hci_h4_init(htr, reverse)	0
#define hci_h4_deinit(htr)
#endif

#endif /* __SLSI_HCI_H4_H__ */
