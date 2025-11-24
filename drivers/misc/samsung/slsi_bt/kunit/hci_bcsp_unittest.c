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
#define FALL_THROUGH()

/******************************************************************************
 * Test target code
 ******************************************************************************/
#include "../hci_bcsp.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
static int ignore_skb(struct hci_trans *htr, struct sk_buff *skb)
{
	BT_INFO("Ignore skb\n");
	kfree_skb(skb);
	return 0;
}

static struct work_struct mock_work;
static struct sk_buff_head mock_q;
static struct hci_trans *mock_receiver;
static void mock_loopback(struct work_struct *work)
{
	struct sk_buff *skb = NULL;

	while (skb = skb_dequeue(&mock_q)) {
		if (skb->len == 1) {
			BT_INFO("%d ms delay\n", skb->data[0]*10);
			msleep(skb->data[0]*10);
			kfree_skb(skb);
			BT_INFO("keep goging\n");
			continue;
		}
		mock_receiver->recv(mock_receiver, skb->data, skb->len, 0);
	}
}

static void mock_add_loopback_delay(int ms)
{
	struct sk_buff *skb = alloc_skb(1, GFP_ATOMIC);

	skb_put_u8(skb, ms/10);
	skb_queue_tail(&mock_q, skb);
	BT_INFO("add %d ms delay\n", ms);
}

static void mock_add_link_msg(int msg)
{
	struct sk_buff *skb = NULL, *slip = NULL;
	
	skb = bcsp_link_msg_build(&slsi_bt_bcsp, msg);
	if (msg == BCSP_LINK_MSG_CONF_RSP) {
		skb_put_u8(skb, 0x3F); // add config
	}
	bcsp_skb_update_packet(&slsi_bt_bcsp, skb);
	slip = slip_encode(skb->data, skb->len, slsi_bt_bcsp.sw_fctrl);
	skb_queue_tail(&mock_q, slip);
	BT_INFO("send message: %s\n", message_to_str[msg]);
}

static void mock_update_packet(struct sk_buff *skb, char seq, char ack)
{
	char bseq = slsi_bt_bcsp.seq;
	char back = slsi_bt_bcsp.ack;

	slsi_bt_bcsp.seq = seq;
	slsi_bt_bcsp.ack = ack;
	bcsp_skb_update_packet(&slsi_bt_bcsp, skb);
	slsi_bt_bcsp.seq = bseq;
	slsi_bt_bcsp.ack = back;
}

static int mock_test_scnario(struct hci_trans *htr, struct sk_buff *slip_skb)
{
	struct hci_trans *receiver = hci_trans_get_prev(htr);
	struct sk_buff *skb = NULL, *slip = NULL;
	unsigned char type;
	static char seq, ack;

	BT_INFO("\n");

	skb = slip_decode(slip_skb->data, slip_skb->len);
	type = skb->data[1] & 0xF;

	if (type == HCI_BCSP_TYPE_LINK_CONTROL) {
		int msg = bcsp_link_get_msg(skb);

		BT_INFO("Link control message\n");
		if (msg == BCSP_LINK_MSG_CONF_RSP) {
			mock_add_link_msg(BCSP_LINK_MSG_CONF_RSP);
			mock_add_link_msg(BCSP_LINK_MSG_SLEEP);
			seq = ack = 0;

		} else if (msg == BCSP_LINK_MSG_WAKEUP) {
			mock_add_link_msg(BCSP_LINK_MSG_WOKEN);

		} else if (msg == BCSP_LINK_MSG_WOKEN) {
		} else {
			skb_queue_tail(&mock_q, slip_skb);
			BT_INFO("send same message: %s\n", message_to_str[msg]);
		}

		mock_receiver = receiver;
		schedule_work(&mock_work);
		kfree_skb(skb);
		return 0;

	/* CMD type used to normal test */
	} else if (type == HCI_BCSP_TYPE_CMD) {
		BT_INFO("Normal send(CMD)/recv(EVT) test\n");

		mock_add_link_msg(BCSP_LINK_MSG_WAKEUP);
		mock_add_loopback_delay(10);

		SET_HCI_PKT_TYPE(skb, HCI_BCSP_TYPE_VENDOR);
		ack = (ack + 1) % 8;
		mock_update_packet(skb, seq++, ack++);
		seq %= 8;
		slip = slip_encode(skb->data, skb->len, slsi_bt_bcsp.sw_fctrl);
		skb_queue_tail(&mock_q, slip);
		BT_INFO("send same data to event\n");

		mock_receiver = receiver;
		schedule_work(&mock_work);
		kfree_skb(skb);
		return 0;

	/* ACL type used to WAKEUP timeout test */
	} else if (type == HCI_BCSP_TYPE_ACL) {
		BT_INFO("Timeout test. ignore packets\n");

	/* Vendor type used to fastpath test */
	} else if (type == HCI_BCSP_TYPE_VENDOR) {
		BT_INFO("Vendor type test. loopback\n");
		SET_HCI_PKT_TYPE(skb, HCI_BCSP_TYPE_VENDOR);
		ack = (ack + 1) % 8;
		mock_update_packet(skb, seq++, ack);
		seq %= 8;
		slip = slip_encode(skb->data, skb->len, slsi_bt_bcsp.sw_fctrl);
		skb_queue_tail(&mock_q, slip);
		BT_INFO("send same data to event\n");

		mock_receiver = receiver;
		schedule_work(&mock_work);
		kfree_skb(skb);
		return 0;
	}


	kfree_skb(skb);
	return ignore_skb(htr, slip_skb);
}

static int test_hci_bcsp_init(struct kunit *test)
{
	kunit_info(test, "hci_bcsp init\n");
	hci_bcsp_init();
	return 0;
}

static void test_hci_bcsp_exit(struct kunit *test)
{
	kunit_info(test, "hci_bcsp exit\n");
	hci_bcsp_deinit();
}

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void hci_bcsp_open_test(struct kunit *test)
{
	struct hci_trans *htr = hci_trans_new("bcsp test open");

	kunit_info(test, "hci_bcsp_open test case\n");

	KUNIT_EXPECT_EQ(test, -EINVAL, hci_bcsp_open(NULL));
	KUNIT_EXPECT_EQ(test, 0, hci_bcsp_open(htr));

	hci_trans_free(htr);

	KUNIT_SUCCEED(test);
}

static void hci_bcsp_send_test(struct kunit *test)
{
	struct hci_trans *htr = hci_trans_new("bcsp");
	struct hci_trans *tail = hci_trans_new("next");
	struct hci_trans *head = hci_trans_new("prev");
	struct sk_buff *skb = NULL;
	char tdata[] = { 0x00, 0x00, 0x06, 0xFF,
			/* slip encode */ 0x11, 0x13, 0xc0, 0xdb,
					   0xFF };

	kunit_info(test, "hci_bcsp test case\n");
	// setup
	INIT_WORK(&mock_work, mock_loopback);
	skb_queue_head_init(&mock_q);
	hci_trans_add_tail(htr, head);
	hci_trans_add_tail(tail, head);
	head->recv_skb = ignore_skb;
	tail->send_skb = mock_test_scnario;

	hci_bcsp_open(htr);
	msleep(100);	// wait for link establishment

	// reliable packet (command)
	BT_INFO("send command\n");
	skb = alloc_hci_pkt_skb(sizeof(tdata));
	skb_put_data(skb, tdata, sizeof(tdata));

	/* cmd type is used to normal working test */
	SET_HCI_PKT_TYPE(skb, HCI_COMMAND_PKT);

	KUNIT_EXPECT_EQ(test, 0, head->send_skb(head, skb));
	msleep(1000);    // wait for sending packet

	hci_trans_del(tail);
	hci_trans_del(htr);
	hci_trans_free(tail);
	hci_trans_free(htr);
	hci_trans_free(head);
	cancel_work_sync(&mock_work);
	skb_queue_purge(&mock_q);
	KUNIT_SUCCEED(test);
}

static void hci_bcsp_timeout_test(struct kunit *test)
{
	struct hci_trans *htr = hci_trans_new("bcsp");
	struct hci_trans *tail = hci_trans_new("next");
	struct sk_buff *skb = NULL;
	char tdata[] = { 0x00, 0x00, 0x01, 0xFF };

	kunit_info(test, "hci_bcsp_timeout test case\n");
	// setup
	INIT_WORK(&mock_work, mock_loopback);
	skb_queue_head_init(&mock_q);
	hci_trans_add_tail(tail, htr);
	tail->send_skb = mock_test_scnario;

	hci_bcsp_open(htr);
	msleep(100);	// wait for link establishment

	// igrnore after open
	tail->send_skb = ignore_skb;

	// reliable packet (command)
	BT_INFO("send data\n");
	skb = alloc_hci_pkt_skb(sizeof(tdata));
	skb_put_data(skb, tdata, sizeof(tdata));

	/* acl used to test wakeup timeout */
	SET_HCI_PKT_TYPE(skb, HCI_BCSP_TYPE_ACL);

	KUNIT_EXPECT_EQ(test, 0, htr->send_skb(htr, skb));
	msleep(100);    // wakeup timeout
	tail->send_skb = mock_test_scnario;
	msleep(500);    // resend timeout

	hci_trans_del(tail);
	hci_trans_free(tail);
	hci_trans_free(htr);
	cancel_work_sync(&mock_work);
	skb_queue_purge(&mock_q);

	KUNIT_SUCCEED(test);
}

static void hci_bcsp_vspmp_fastpath_test(struct kunit *test)
{
	struct hci_trans *htr = hci_trans_new("bcsp");
	struct hci_trans *tail = hci_trans_new("next");
	struct sk_buff *skb = NULL;
	char tdata_cfg[] = { VSPMP_CHANNEL_FASTPATH,
		FASTPATH_ENABLE_CFM_TAG, 1, true };
	char tdata_active[] = { VSPMP_CHANNEL_FASTPATH, 
		FASTPATH_TO_AIR_ACTIVATE_IND_TAG, 1, true};
	char tdata_datagram[] = { VSPMP_CHANNEL_FASTPATH,
		FASTPATH_DATAGRAM_TAG, 7,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	kunit_info(test, "hci_bcsp_vspmp_fastpath test case\n");
	// setup
	INIT_WORK(&mock_work, mock_loopback);
	skb_queue_head_init(&mock_q);
	hci_trans_add_tail(tail, htr);
	tail->send_skb = mock_test_scnario;

	hci_bcsp_open(htr);
	msleep(100);	// wait for link establishment

	// reliable packet (command)
	kunit_info(test, "send fastpath_cfg\n");
	skb = alloc_hci_pkt_skb(sizeof(tdata_cfg));
	skb_put_data(skb, tdata_cfg, sizeof(tdata_cfg));
	SET_HCI_PKT_TYPE(skb, HCI_BCSP_TYPE_VENDOR);
	KUNIT_EXPECT_EQ(test, 0, htr->send_skb(htr, skb));
	msleep(10);

	kunit_info(test, "send fastpath_active\n");
	skb = alloc_hci_pkt_skb(sizeof(tdata_active));
	skb_put_data(skb, tdata_active, sizeof(tdata_active));
	SET_HCI_PKT_TYPE(skb, HCI_BCSP_TYPE_VENDOR);
	KUNIT_EXPECT_EQ(test, 0, htr->send_skb(htr, skb));
	msleep(10);

	kunit_info(test, "send fastpath_datagram\n");
	skb = alloc_hci_pkt_skb(sizeof(tdata_datagram));
	skb_put_data(skb, tdata_datagram, sizeof(tdata_datagram));
	SET_HCI_PKT_TYPE(skb, HCI_BCSP_TYPE_VENDOR);
	KUNIT_EXPECT_EQ(test, 0, htr->send_skb(htr, skb));
	msleep(10);

	hci_trans_del(tail);
	hci_trans_free(tail);
	hci_trans_free(htr);
	cancel_work_sync(&mock_work);
	skb_queue_purge(&mock_q);

	KUNIT_SUCCEED(test);
}

#if IS_ENABLED(CONFIG_SLSI_BT_FASTPATH)
extern int test_slsi_bt_fastpath_init(struct kunit *test);
extern void test_slsi_bt_fastpath_exit(struct kunit *test);
static void hci_bcsp_fastpath_info_test(struct kunit *test)
{
	struct hci_trans *htr = hci_trans_new("bcsp");
	struct sk_buff *skb = NULL;
	char tdata[3 + FASTPATH_DATAGRAM_LEN] =
		{ 0x00, 0x00, FASTPATH_DATAGRAM_LEN, 0x00, };
	int q_cnt = 0;

	kunit_info(test, "hci_bcsp_fastpath_info test case\n");
	hci_bcsp_open(htr);
	msleep(100);	// wait for link establishment

	test_slsi_bt_fastpath_init(test);
	q_cnt = skb_queue_len(&slsi_bt_bcsp.rel_q);
	fastpath_enable(&slsi_bt_bcsp, true);
	KUNIT_EXPECT_EQ(test, q_cnt+1, skb_queue_len(&slsi_bt_bcsp.rel_q));

	// packet
	skb = alloc_hci_pkt_skb(sizeof(tdata));
	skb_put_data(skb, tdata, sizeof(tdata));
	SET_HCI_PKT_TYPE(skb, HCI_COMMAND_PKT);

	skb = change_to_fastpath(skb);
	KUNIT_EXPECT_EQ(test, HCI_PROPERTY_PKT, GET_HCI_PKT_TYPE(skb));
	KUNIT_EXPECT_EQ(test, VSPMP_CHANNEL_FASTPATH, skb->data[0]);

	kfree_skb(skb);

	test_slsi_bt_fastpath_exit(test);
	hci_trans_free(htr);
	KUNIT_SUCCEED(test);
}
#endif

static void hci_bcsp_proc_show_test(struct kunit *test)
{
	struct file *file = kunit_kmalloc(test, sizeof(struct file), GFP_KERNEL);
	struct seq_file *s;
#define PROC_TEST_BUF_SIZE      (1024*10)
	char *buf = kunit_kmalloc(test, PROC_TEST_BUF_SIZE, GFP_USER);
	struct hci_trans *htr = hci_trans_new("bcsp test open");

	kunit_info(test, "hci_bcsp_proc_show test case\n");

	single_open(file, hci_bcsp_proc_show, NULL);

	// fake seq_file
	s = file->private_data;
	s->count = 0;
	s->size = PROC_TEST_BUF_SIZE;
	s->buf = buf;

	hci_bcsp_open(htr);
	hci_bcsp_proc_show(htr, s);

	kunit_kfree(test, buf);
	kunit_kfree(test, file);
	hci_trans_free(htr);

	KUNIT_SUCCEED(test);
}

static struct kunit_case hci_bcsp_test_cases[] = {
	KUNIT_CASE(hci_bcsp_open_test),
	KUNIT_CASE(hci_bcsp_send_test),
	KUNIT_CASE(hci_bcsp_timeout_test),
	KUNIT_CASE(hci_bcsp_vspmp_fastpath_test),
#if IS_ENABLED(CONFIG_SLSI_BT_FASTPATH)
	KUNIT_CASE(hci_bcsp_fastpath_info_test),
#endif
	KUNIT_CASE(hci_bcsp_proc_show_test),
	{}
};

static struct kunit_suite hci_bcsp_test_suite = {
	.name = "hci_bcsp_unittest",
	.test_cases = hci_bcsp_test_cases,
	.init = test_hci_bcsp_init,
	.exit = test_hci_bcsp_exit,
};

kunit_test_suite(hci_bcsp_test_suite);
