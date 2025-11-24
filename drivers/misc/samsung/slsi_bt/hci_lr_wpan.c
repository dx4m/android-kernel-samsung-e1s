/******************************************************************************
 *                                                                            *
 * Copyright (c) 2024 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * LR-WPAN                                                                    *
 *                                                                            *
 ******************************************************************************/

#include <linux/skbuff.h>
#include <linux/seq_file.h>

#include "hci_trans.h"
#include "hci_pkt.h"
#include "hci_lr_wpan.h"

struct hci_lr_wpan {
	struct sk_buff	        *rskb;
	struct hci_trans        *htr_prev;
};

inline struct sk_buff *alloc_hci_lr_wpan_pkt_skb(int size)
{
	struct sk_buff *skb = alloc_hci_pkt_skb(size + HCI_LR_WPAN_CH_SIZE);
	skb_reserve(skb, HCI_LR_WPAN_CH_SIZE); // Make space for channel
	if (skb) {
		SET_HCI_PKT_TYPE(skb, HCI_PROPERTY_PKT);
		SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_LR_WPAN);
	} else
		BT_WARNING("allocation failed\n");

	return skb;
}

static inline struct hci_lr_wpan *get_hci_lr_wpan(struct hci_trans *htr)
{
	return htr ? (struct hci_lr_wpan *)htr->tdata : NULL;
}

static int hci_lr_wpan_pkt_status(char *data, size_t len)
{
	char length;

	if (!data)
		return HCI_PKT_STATUS_NO_DATA;

	if (len < HCI_LR_WPAN_TAG_SIZE + HCI_LR_WPAN_LENGTH_SIZE)
		return HCI_PKT_STATUS_NOT_ENOUGH_LENGTH;

	length = data[HCI_LR_WPAN_LENGTH_OFFSET];
	if (len < length + HCI_LR_WPAN_TAG_SIZE + HCI_LR_WPAN_LENGTH_SIZE)
		return HCI_PKT_STATUS_NOT_ENOUGH_LENGTH;
	else if (len > length + HCI_LR_WPAN_TAG_SIZE + HCI_LR_WPAN_LENGTH_SIZE)
		return HCI_PKT_STATUS_OVERRUN_LENGTH;

	return HCI_PKT_STATUS_COMPLETE;
}

static struct sk_buff *get_collector_skb(struct sk_buff **ref, size_t size)
{
	struct sk_buff *skb;

	if (ref == NULL)
		return NULL;

	skb = *ref;
	if (skb == NULL) {
		skb = alloc_hci_lr_wpan_pkt_skb(size);

		if (skb == NULL)
			BT_ERR("failed to allocate memory\n");
	} else if (skb_tailroom(skb) < size) {
		struct sk_buff *nskb = skb;

		TR_DBG("expand skb with %zu\n", size);
		nskb = skb_copy_expand(skb, 0, size, GFP_ATOMIC);
		if (nskb == NULL)
			BT_ERR("failed to expand skb\n");
		kfree_skb(skb);
		skb = nskb;
	}

	*ref = skb;
	return skb;
}

static int collector_lr_wpan_data(struct sk_buff *skb, const char *data,
				  size_t count)
{
	if (copy_from_user(skb_put(skb, count), data, count) != 0) {
		TR_WARNING("copy_from_user failed\n");
		return -EACCES;
	}

	return 0;
}

static int hci_lr_wpan_add_channel_and_send(struct hci_lr_wpan *lr_wpan,
					    struct sk_buff *skb)
{
	skb_push(skb, 1);
	skb->data[0] = HCI_LR_WPAN_VS_CHANNEL_LR_WPAN;
	SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_HCI);

	// Use the previous htr as that will forward the skb down to property
	// handler
	return lr_wpan->htr_prev->send_skb(lr_wpan->htr_prev, skb);
}

static int hci_lr_wpan_send_skb(struct hci_trans *htr, struct sk_buff *skb)
{
	int ret;
	struct hci_lr_wpan *lr_wpan = get_hci_lr_wpan(htr);

	if (htr == NULL || skb == NULL || lr_wpan == NULL ||
		lr_wpan->htr_prev == NULL)
		return -EINVAL;

	ret = hci_lr_wpan_pkt_status(skb->data, skb->len);
	while (ret == HCI_PKT_STATUS_OVERRUN_LENGTH) {
		char split = HCI_LR_WPAN_TAG_SIZE + HCI_LR_WPAN_LENGTH_SIZE +
			     skb->data[HCI_LR_WPAN_LENGTH_OFFSET];
		lr_wpan->rskb = alloc_hci_lr_wpan_pkt_skb(skb->len - split);
		skb_split(skb, lr_wpan->rskb, split);
		TR_DBG("received %d bytes\n", skb->len);
		if (hci_lr_wpan_add_channel_and_send(lr_wpan, skb) < 0)
			break;
		skb = lr_wpan->rskb;
		ret = hci_lr_wpan_pkt_status(skb->data, skb->len);
	}

	if (ret != HCI_PKT_STATUS_COMPLETE) {
		TR_DBG("LR-WPAN packet is not completed. Wait for more data\n");
		return 0;
	}

	lr_wpan->rskb = NULL;
	return hci_lr_wpan_add_channel_and_send(lr_wpan, skb);
}

static ssize_t hci_lr_wpan_send(struct hci_trans *htr, const char __user *data,
		size_t count, unsigned int flags)
{
	struct hci_lr_wpan *lr_wpan = get_hci_lr_wpan(htr);
	struct sk_buff *skb;
	ssize_t ret = 0;

	TR_DBG("count: %zu\n", count);
	if (htr == NULL|| data == NULL || lr_wpan == NULL)
		return -EINVAL;

	skb = get_collector_skb(&lr_wpan->rskb, count);
	if (skb == NULL) {
		BT_ERR("failed to allocate write skb\n");
		return -ENOMEM;
	}

	if (collector_lr_wpan_data(skb, data, count) != 0) {
		kfree_skb(skb);
		lr_wpan->rskb = NULL;
		return -EACCES;
	}

	TR_DBG("received %d bytes\n", skb->len);
	ret = htr->send_skb(htr, skb);
	return (ret == 0) ? count : ret;
}

int hci_lr_wpan_init(struct hci_trans *htr, struct hci_trans *htr_prev)
{
	struct hci_lr_wpan *lr_wpan;

	htr->send_skb = hci_lr_wpan_send_skb;
	htr->send = hci_lr_wpan_send;
	htr->deinit = hci_lr_wpan_deinit;

	lr_wpan = kzalloc(sizeof(struct hci_lr_wpan), GFP_KERNEL);
	lr_wpan->htr_prev = htr_prev;
	htr->tdata = lr_wpan;

	return 0;
}

void hci_lr_wpan_deinit(struct hci_trans *htr)
{
	struct hci_lr_wpan *lr_wpan = get_hci_lr_wpan(htr);

	if (htr == NULL || lr_wpan == NULL)
		return;

	if (lr_wpan->rskb)
		kfree_skb(lr_wpan->rskb);

	kfree(lr_wpan);
	htr->tdata = NULL;
	hci_trans_deinit(htr);
}
