/******************************************************************************
 *                                                                            *
 * Copyright (c) 2025 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * Bluetooth Driver Unittest for KUNIT                                        *
 *                                                                            *
 ******************************************************************************/
#include <kunit/test.h>


/******************************************************************************
 * Fake tricks
 ******************************************************************************/
#include <linux/uaccess.h>
#define copy_to_user(d, s, n)   (memcpy(d, s, n), 0)
#define copy_from_user(d, s, n) (memcpy(d, s, n), 0)


/******************************************************************************
 * Test target code
 ******************************************************************************/
#include "../hci_lr_wpan.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
#include <linux/fs.h>
#include <linux/poll.h>
#include "../slsi_bt_property.h"

struct test_helper_lr_wpan_st {
	struct file_operations *fops;
	ssize_t (*wait_for_data)(void);
	struct hci_trans *htr;
} *lr_wpan;
void *test_helper_get_lr_wpan_context(void);

int hci_lr_wpan_test__init(struct kunit *test)
{
	lr_wpan = test_helper_get_lr_wpan_context();
	return 0;
}

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void hci_lr_wpan_vs_lr_wpan_handler_test(struct kunit *test)
{
	struct hci_trans *htr;
	struct file fake_file;
	char tdata[] = {0, 2, 0x00, 0x00, 1, 0};
	struct sk_buff *skb;

	kunit_info(test, "hci_lr_wpan_vs_lr_wpan_handler test case\n");

	// write
	KUNIT_EXPECT_EQ(test, 0, lr_wpan->fops->open(NULL, NULL));
	KUNIT_EXPECT_EQ(test,
			sizeof(tdata),
			lr_wpan->fops->write(NULL, tdata, sizeof(tdata), NULL));

	// recv (slsi_demultiplex_skb_recv)
	skb = alloc_skb(sizeof(tdata), GFP_ATOMIC);
	skb_put_u8(skb, SLSI_BTP_VS_CHANNEL_LR_WPAN);
	skb_put_data(skb, tdata, sizeof(tdata));
	SET_HCI_PKT_TYPE(skb, HCI_PROPERTY_PKT);
	SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_HCI);

	lr_wpan = test_helper_get_lr_wpan_context(); // update
	htr = ((struct hci_lr_wpan*)lr_wpan->htr->tdata)->htr_prev;
	htr = hci_trans_get_next(htr);
	BT_INFO("0 --- %s\n", hci_trans_get_name(htr)); // property type
	KUNIT_EXPECT_EQ(test, htr->recv_skb(htr, skb), sizeof(tdata));

	BT_INFO("1\n");
	// poll
	KUNIT_EXPECT_EQ(test,
			POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM,
			lr_wpan->fops->poll(NULL, NULL));

	BT_INFO("2\n");
	KUNIT_EXPECT_EQ(test, 0, lr_wpan->wait_for_data());

	BT_INFO("3\n");
	// read
	KUNIT_EXPECT_EQ(test,
			sizeof(tdata),
			lr_wpan->fops->read(
				&fake_file, tdata, sizeof(tdata), NULL));

	BT_INFO("4\n");
	KUNIT_EXPECT_EQ(test, 0, lr_wpan->fops->release(NULL, NULL));
	KUNIT_SUCCEED(test);
}


static struct kunit_case hci_lr_wpan_test_cases[] = {
	KUNIT_CASE(hci_lr_wpan_vs_lr_wpan_handler_test),
	{}
};

static struct kunit_suite hci_lr_wpan_test_suite = {
	.name = "hci_lr_wpan_unittest",
	.test_cases = hci_lr_wpan_test_cases,
	.init = hci_lr_wpan_test__init,
};

kunit_test_suite(hci_lr_wpan_test_suite);
