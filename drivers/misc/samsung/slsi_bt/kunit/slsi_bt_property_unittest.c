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
#include "../slsi_bt_property.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
static struct hci_trans *htr = NULL;

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void slsi_bt_property_init_test(struct kunit *test)
{
	kunit_info(test, "slsi_bt_property_init test case\n");

	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_bt_property_init(NULL));

	htr = hci_trans_new("property_test");
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_property_init(htr));

	KUNIT_SUCCEED(test);
}

static void slsi_bt_property_recv_hook_test(struct kunit *test)
{
	struct sk_buff *skb = NULL;
	char tdata[]  = { HCI_PROPERTY_PKT, SLSI_BTP_VS_CHANNEL_UNKNOWN };
	char tdata2[] = { HCI_PROPERTY_PKT, SLSI_BTP_VS_CHANNEL_MXLOG,
			  /* T */ MXLOG_LOG_EVENT_IND,
			  /* L */ 1
			};
	char tdata3[] = { HCI_PROPERTY_PKT, SLSI_BTP_VS_CHANNEL_MXLOG,
			  /* T */ MXLOG_LOG_EVENT_IND,
			  /* L */ 1,
			  /* V */ 0x00
			};
	char tdata4[] = { HCI_PROPERTY_PKT, SLSI_BTP_VS_CHANNEL_MXLOG,
			  /* T */ 0,
			  /* L */ 1,
			  /* V */ 0x00
			};

	kunit_info(test, "slsi_bt_property test case\n");
	// recv. It is HCI_PROPERTY_PKT type
	KUNIT_EXPECT_EQ(test, -EINVAL, htr->recv(htr, tdata, sizeof(tdata), 0));

	// recv SLSI_BTP_VS_CHANNEL_UNKNOWN channel
	skb = alloc_hci_h4_pkt_skb(sizeof(tdata));
	skb_put_data(skb, tdata, sizeof(tdata));
	SET_HCI_PKT_TYPE(skb, HCI_PROPERTY_PKT);
	KUNIT_EXPECT_EQ(test, -EINVAL, htr->recv_skb(htr, skb));

	// recv SLSI_BTP_VS_CHANNEL_MXLOG channel with 0 parameter
	skb = alloc_hci_h4_pkt_skb(sizeof(tdata2));
	skb_put_data(skb, tdata2, 2);
	SET_HCI_PKT_TYPE(skb, HCI_PROPERTY_PKT);
	KUNIT_EXPECT_EQ(test, 0, htr->recv_skb(htr, skb));

	// recv SLSI_BTP_VS_CHANNEL_MXLOG channel with no value
	skb = alloc_hci_h4_pkt_skb(sizeof(tdata2));
	skb_put_data(skb, tdata2, sizeof(tdata2));
	SET_HCI_PKT_TYPE(skb, HCI_PROPERTY_PKT);
	KUNIT_EXPECT_EQ(test, -EINVAL, htr->recv_skb(htr, skb));

	// recv SLSI_BTP_VS_CHANNEL_MXLOG channel
	skb = alloc_hci_h4_pkt_skb(sizeof(tdata3));
	skb_put_data(skb, tdata3, sizeof(tdata3));
	SET_HCI_PKT_TYPE(skb, HCI_PROPERTY_PKT);
	KUNIT_EXPECT_EQ(test, 0, htr->recv_skb(htr, skb));

	// recv SLSI_BTP_VS_CHANNEL_MXLOG channel with unknown tag
	skb = alloc_hci_h4_pkt_skb(sizeof(tdata4));
	skb_put_data(skb, tdata4, sizeof(tdata4));
	SET_HCI_PKT_TYPE(skb, HCI_PROPERTY_PKT);
	// It tries to response to received packet but it will be error
	KUNIT_EXPECT_EQ(test, -EINVAL, htr->recv_skb(htr, skb));

	KUNIT_SUCCEED(test);
}

static void slsi_bt_property_write_hook_test(struct kunit *test)
{
	struct sk_buff *skb = NULL;
	char tdata[]  = { 0x01, 0x02, 0x03, 0x04 };
#if IS_ENABLED(CONFIG_SLSI_BT_FWLOG_SNOOP)
	struct {
		unsigned short opcode;
		unsigned char  length;
		unsigned char  enabled;
		unsigned int   filters[4];
	} tdata_btsnoop = {
		.opcode = HCI_VSC_FWLOG_BTSNOOP,
		.length = 17,
		.enabled = true,
	};
#endif

	kunit_info(test, "slsi_bt_property test case\n");
	skb = alloc_hci_pkt_skb(sizeof(tdata));
	skb_put_data(skb, tdata, sizeof(tdata));
	SET_HCI_PKT_TYPE(skb, HCI_COMMAND_PKT);
	KUNIT_EXPECT_EQ(test, -EINVAL, htr->send_skb(htr, skb));

#if IS_ENABLED(CONFIG_SLSI_BT_FWLOG_SNOOP)
	kunit_info(test, "FWLOG_SNOOP test case\n");
	// enable fwsnoop
	skb = alloc_hci_pkt_skb(sizeof(tdata_btsnoop));
	skb_put_data(skb, (void *)&tdata_btsnoop, sizeof(tdata_btsnoop));
	SET_HCI_PKT_TYPE(skb, HCI_COMMAND_PKT);
	KUNIT_EXPECT_EQ(test, sizeof(tdata_btsnoop), htr->send_skb(htr, skb));

	// disable fwsnoop
	tdata_btsnoop.enabled = false;
	skb = alloc_hci_pkt_skb(sizeof(tdata_btsnoop));
	skb_put_data(skb, (void *)&tdata_btsnoop, sizeof(tdata_btsnoop));
	SET_HCI_PKT_TYPE(skb, HCI_COMMAND_PKT);
	KUNIT_EXPECT_EQ(test, sizeof(tdata_btsnoop), htr->send_skb(htr, skb));
#endif

	KUNIT_SUCCEED(test);
}

static void slsi_bt_property_set_logmask_test(struct kunit *test)
{
	unsigned int val[] = { 0x01, 0x02 };
	kunit_info(test, "slsi_bt_property_set_logmask test case\n");
	KUNIT_EXPECT_EQ(test,
			-EINVAL,
			slsi_bt_property_set_logmask(htr, val, 1));
	KUNIT_EXPECT_EQ(test,
			-EINVAL,
			slsi_bt_property_set_logmask(htr, val, 2));
	KUNIT_SUCCEED(test);
}

static void slsi_bt_property_proc_show_test(struct kunit *test)
{
	struct file *file = kunit_kmalloc(test, sizeof(struct file), GFP_KERNEL);
	struct seq_file *s;
#define PROC_TEST_BUF_SIZE      (1024*10)
	char *buf = kunit_kmalloc(test, PROC_TEST_BUF_SIZE, GFP_USER);

	kunit_info(test, "slsi_bt_property_proc_show test case\n");

	single_open(file, property_proc_show, NULL);

	// fake seq_file
	s = file->private_data;
	s->count = 0;
	s->size = PROC_TEST_BUF_SIZE;
	s->buf = buf;

	property_proc_show(htr, s);

	kunit_kfree(test, buf);
	kunit_kfree(test, file);


	KUNIT_SUCCEED(test);
}

static void slsi_bt_property_deinit_test(struct kunit *test)
{
	kunit_info(test, "slsi_bt_property_deinit test case\n");

	hci_trans_free(htr);
	htr = NULL;
	KUNIT_SUCCEED(test);
}

static struct kunit_case slsi_bt_property_test_cases[] = {
	KUNIT_CASE(slsi_bt_property_init_test),
	KUNIT_CASE(slsi_bt_property_recv_hook_test),
	KUNIT_CASE(slsi_bt_property_write_hook_test),
	KUNIT_CASE(slsi_bt_property_set_logmask_test),
	KUNIT_CASE(slsi_bt_property_proc_show_test),
	KUNIT_CASE(slsi_bt_property_deinit_test),
	{}
};

static struct kunit_suite slsi_bt_property_test_suite = {
	.name = "slsi_bt_property_unittest",
	.test_cases = slsi_bt_property_test_cases,
};

kunit_test_suite(slsi_bt_property_test_suite);
