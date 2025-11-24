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

/******************************************************************************
 * Test target code
 ******************************************************************************/
#include "../slsi_bt_fastpath.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
static save_disable_fastpath_support;

extern int test_slsi_bt_controller_init(struct kunit *test);
extern void test_slsi_bt_controller_exit(struct kunit *test);
int test_slsi_bt_fastpath_init(struct kunit *test)
{
	kunit_info(test, "slsi_bt_fastpath init\n");
	test_slsi_bt_controller_init(test);
	slsi_bt_controller_start(SCSC_SERVICE_ID_BT);
	save_disable_fastpath_support = disable_fastpath_support;
	disable_fastpath_support = false;
	return 0;
}

void test_slsi_bt_fastpath_exit(struct kunit *test)
{
	kunit_info(test, "slsi_bt_fastpath exit\n");
	slsi_bt_fastpath_activate(false);
	disable_fastpath_support = save_disable_fastpath_support;
	slsi_bt_controller_stop(SCSC_SERVICE_ID_BT);
	test_slsi_bt_controller_exit(test);
}

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void slsi_bt_fastpath_available_test(struct kunit *test)
{
	struct sk_buff *skb = NULL;
	char tdata[3 + FASTPATH_DATAGRAM_LEN] =
		{ 0x00, 0x00, FASTPATH_DATAGRAM_LEN, 0x00, };

	kunit_info(test, "slsi_bt_fastpath_available test case\n");

	skb = alloc_hci_pkt_skb(sizeof(tdata));
	skb_put_data(skb, tdata, sizeof(tdata));
	SET_HCI_PKT_TYPE(skb, HCI_COMMAND_PKT);

	disable_fastpath_support = true;
	KUNIT_EXPECT_FALSE(test, slsi_bt_fastpath_supported());

	slsi_bt_fastpath_activate(true);
	KUNIT_EXPECT_FALSE(test, slsi_bt_fastpath_available(skb));

	disable_fastpath_support = false;
	KUNIT_EXPECT_TRUE(test, slsi_bt_fastpath_supported());

	slsi_bt_fastpath_activate(false);
	KUNIT_EXPECT_FALSE(test, slsi_bt_fastpath_available(skb));

	slsi_bt_fastpath_activate(true);
	KUNIT_EXPECT_TRUE(test, slsi_bt_fastpath_available(skb));

	kfree_skb(skb);
	KUNIT_SUCCEED(test);
}

static void slsi_bt_fastpath_write_read_test(struct kunit *test)
{
	struct fastpath_datagram datagram;
	struct sk_buff *skb = NULL;
	char tdata[3 + FASTPATH_DATAGRAM_LEN] =
		{ 0x00, 0x00, FASTPATH_DATAGRAM_LEN, 0x00, };

	// setup
	slsi_bt_fastpath_activate(true);
	skb = alloc_hci_pkt_skb(sizeof(tdata));
	skb_put_data(skb, tdata, sizeof(tdata));
	SET_HCI_PKT_TYPE(skb, HCI_COMMAND_PKT);

	// write
	fpshm_bufpool_enabled = false;
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_fastpath_write(skb, &datagram));
	KUNIT_EXPECT_EQ(test, datagram.type, HCI_COMMAND_PKT);
	KUNIT_EXPECT_EQ(test, datagram.length, sizeof(tdata));
	KUNIT_EXPECT_TRUE(test, datagram.location);
	kfree_skb(skb);

	skb = slsi_bt_fastpath_datagram_packet(&datagram);
	KUNIT_ASSERT_NOT_NULL(test, skb);

	slsi_bt_fastpath_update_key(skb->data, skb->len, 0);

	slsi_bt_fastpath_acknowledgement(0);
	kfree_skb(skb);

	// read
	skb = slsi_bt_fastpath_read(&datagram);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	KUNIT_EXPECT_EQ(test, HCI_COMMAND_PKT, GET_HCI_PKT_TYPE(skb));
	KUNIT_EXPECT_EQ(test, 0x00, skb->data[0]);
	KUNIT_EXPECT_EQ(test, 0x00, skb->data[1]);
	kfree(skb);

	skb = alloc_hci_pkt_skb(sizeof(tdata));
	skb_put_data(skb, tdata, sizeof(tdata));
	SET_HCI_PKT_TYPE(skb, HCI_COMMAND_PKT);

	// once again for acknowledgement_all
	fpshm_bufpool_enabled = true;
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_fastpath_write(skb, &datagram));
	KUNIT_EXPECT_EQ(test, datagram.type, HCI_COMMAND_PKT);
	KUNIT_EXPECT_EQ(test, datagram.length, sizeof(tdata));
	KUNIT_EXPECT_TRUE(test, datagram.location);
	kfree_skb(skb);

	skb = slsi_bt_fastpath_datagram_packet(&datagram);
	KUNIT_ASSERT_NOT_NULL(test, skb);

	slsi_bt_fastpath_update_key(skb->data, skb->len, 0);
	slsi_bt_fastpath_acknowledgement_all();
	kfree_skb(skb);

	// read
	skb = slsi_bt_fastpath_read(&datagram);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	KUNIT_EXPECT_EQ(test, HCI_COMMAND_PKT, GET_HCI_PKT_TYPE(skb));
	KUNIT_EXPECT_EQ(test, 0x00, skb->data[0]);
	KUNIT_EXPECT_EQ(test, 0x00, skb->data[1]);
	kfree(skb);

	KUNIT_SUCCEED(test);
}


static void slsi_bt_fastpath_proc_show_test(struct kunit *test)
{
	struct file *file = kunit_kmalloc(test, sizeof(struct file), GFP_KERNEL);
	struct seq_file *s;
#define PROC_TEST_BUF_SIZE      (1024*10)
	char *buf = kunit_kmalloc(test, PROC_TEST_BUF_SIZE, GFP_USER);
	struct hci_trans *htr = hci_trans_new("bcsp test open");

	kunit_info(test, "hci_bcsp_proc_show test case\n");

	single_open(file, slsi_bt_fastpath_proc_show, NULL);

	// fake seq_file
	s = file->private_data;
	s->count = 0;
	s->size = PROC_TEST_BUF_SIZE;
	s->buf = buf;

	slsi_bt_fastpath_proc_show(s);

	kunit_kfree(test, buf);
	kunit_kfree(test, file);

	KUNIT_SUCCEED(test);
}

static void slsi_bt_fastpath_1(struct kunit *test)
{
	kunit_info(test, "slsi_bt_fastpath test case\n");
	KUNIT_SUCCEED(test);
}

static struct kunit_case slsi_bt_fastpath_test_cases[] = {
	KUNIT_CASE(slsi_bt_fastpath_available_test),
	KUNIT_CASE(slsi_bt_fastpath_write_read_test),
	KUNIT_CASE(slsi_bt_fastpath_proc_show_test),
	{}
};

static struct kunit_suite slsi_bt_fastpath_test_suite = {
	.name = "slsi_bt_fastpath_unittest",
	.test_cases = slsi_bt_fastpath_test_cases,
	.init = test_slsi_bt_fastpath_init,
	.exit = test_slsi_bt_fastpath_exit,
};

kunit_test_suite(slsi_bt_fastpath_test_suite);
