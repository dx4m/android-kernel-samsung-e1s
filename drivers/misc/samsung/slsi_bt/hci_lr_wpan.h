/******************************************************************************
 *                                                                            *
 * Copyright (c) 2024 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * LR-WPAN                                                                    *
 *                                                                            *
 ******************************************************************************/
#ifndef __HCI_LR_WPAN_H__
#define __HCI_LR_WPAN_H__

#include "hci_pkt.h"
#include "hci_trans.h"

#define HCI_LR_WPAN_TAG_HW_ERROR        0x3
#define HCI_LR_WPAN_VS_CHANNEL_LR_WPAN  3

#define HCI_LR_WPAN_CH_SIZE          1
#define HCI_LR_WPAN_TAG_SIZE         1
#define HCI_LR_WPAN_LENGTH_SIZE      1

#define HCI_LR_WPAN_TAG_OFFSET       0
#define HCI_LR_WPAN_LENGTH_OFFSET    1

inline struct sk_buff *alloc_hci_lr_wpan_pkt_skb(int size);

int hci_lr_wpan_init(struct hci_trans *htr, struct hci_trans *htr_prev);
void hci_lr_wpan_deinit(struct hci_trans *htr);

#endif /* __HCI_LR_WPAN_H__ */
