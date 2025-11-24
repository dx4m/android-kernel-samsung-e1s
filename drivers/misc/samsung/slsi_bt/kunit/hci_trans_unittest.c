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
#define copy_from_user(d, s, n) (memcpy(d, s, n), 0)

/******************************************************************************
 * Test target code
 ******************************************************************************/
#include "../hci_trans.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
static int tester_flags = 0;
static int loopback_tester(struct hci_trans *htr, struct sk_buff *skb)
{
	int ret = skb->len;

	htr->recv(htr, skb->data, skb->len, tester_flags);
	kfree_skb(skb);

	return ret;
}

static int tester_received_len = 0;
static int receive_tester(struct hci_trans *htr, struct sk_buff *skb)
{
	int ret = skb->len;

	kfree_skb(skb);
	tester_received_len += ret;

	return ret;
}

static int tester_suspend_cnt = 0;
static int suspend_tester(struct hci_trans *htr)
{
	tester_suspend_cnt++;
	return 0;
}

static int suspend_fail_tester(struct hci_trans *htr)
{
	suspend_tester(htr);
	return -EBUSY;
}

static int tester_resume_cnt = 0;
static int resume_tester(struct hci_trans *htr)
{
	tester_resume_cnt++;
	return 0;
}

static void deinit_tester(struct hci_trans *htr)
{
	struct kunit *test = htr->tdata;

	kunit_info(test, "deinit tester\n");
}
/******************************************************************************
 * Test cases
 ******************************************************************************/
struct hci_trans *head = NULL, *tail = NULL;
static void hci_trans_new_test(struct kunit *test)
{
	struct hci_trans *htr = NULL;

	kunit_info(test, "hci_trans_new test case\n");
	KUNIT_ASSERT_NOT_NULL(test, head = hci_trans_new("test head"));
	head->recv_skb = receive_tester;

	KUNIT_ASSERT_NOT_NULL(test, htr = hci_trans_new("test htr 1"));
	htr->deinit = deinit_tester;
	htr->tdata = test;
	hci_trans_add_tail(htr, head);

	KUNIT_ASSERT_NOT_NULL(test, htr = hci_trans_new(NULL)); // Unknown
	hci_trans_add_tail(htr, head);
	htr->suspend = suspend_tester;
	htr->resume = resume_tester;

	KUNIT_ASSERT_NOT_NULL(test, tail = hci_trans_new("test tail"));
	hci_trans_add_tail(tail, head);
	tail->send_skb = loopback_tester;

	KUNIT_SUCCEED(test);
}

static void hci_trans_send_recv_test(struct kunit *test)
{
	char tdata[] = { 0, 1, 2, 3};
	char trecv[sizeof(tdata)] = { 0, };

	kunit_info(test, "hci_trans_send_recv test case\n");

	tester_flags = 0;
	tester_received_len = 0;

	KUNIT_EXPECT_EQ(test,
			sizeof(tdata),
			head->send(head, tdata, sizeof(tdata), 0));

	KUNIT_EXPECT_EQ(test,
			sizeof(tdata),
			head->send(head, tdata, sizeof(tdata), HCITRFLAG_UBUF));

	KUNIT_EXPECT_EQ(test,
			sizeof(tdata)*2,
			tester_received_len);

	KUNIT_SUCCEED(test);
}

static void hci_trans_suspend_resume_test(struct kunit *test)
{
	kunit_info(test, "hci_trans_suspend_resume_test test case\n");

	tester_suspend_cnt = 0;
	tail->suspend = NULL;
	KUNIT_EXPECT_EQ(test, 0, hci_trans_suspend(head));
	KUNIT_EXPECT_EQ(test, 1, tester_suspend_cnt);

	tester_resume_cnt = 0;
	KUNIT_EXPECT_EQ(test, 0, hci_trans_resume(head));
	KUNIT_EXPECT_EQ(test, 1, tester_resume_cnt);

	tester_suspend_cnt = 0;
	tester_resume_cnt = 0;
	tail->suspend = suspend_fail_tester;
	KUNIT_EXPECT_EQ(test, -EBUSY, hci_trans_suspend(head));
	KUNIT_EXPECT_EQ(test, 2, tester_suspend_cnt);
	KUNIT_EXPECT_EQ(test, 1, tester_resume_cnt);
	KUNIT_SUCCEED(test);
}

static void hci_trans_invalid_argument_test(struct kunit *test)
{
	kunit_info(test, "hci_trans_invalid_argument_test test case\n");

	head->recv_skb = hci_trans_recv_skb;
	tail->send_skb = hci_trans_send_skb;

	head->send_skb(head, NULL);
	tail->recv_skb(tail, NULL);
	KUNIT_SUCCEED(test);
}

static void hci_trans_del_free_test(struct kunit *test)
{
	struct hci_trans *htr = NULL;
	struct list_head *pos = NULL, *tmp = NULL;

	kunit_info(test, "hci_trans_del_free test case\n");
	list_for_each_safe(pos, tmp, &head->list) {
		htr = list_entry(pos, struct hci_trans, list);
		hci_trans_del(htr);
		BT_DBG("free transport: %s\n", hci_trans_get_name(htr));
		hci_trans_free(htr);
	}
	hci_trans_del(head);
	hci_trans_free(head);

	KUNIT_SUCCEED(test);
}

static struct kunit_case hci_trans_test_cases[] = {
	KUNIT_CASE(hci_trans_new_test),
	KUNIT_CASE(hci_trans_send_recv_test),
	KUNIT_CASE(hci_trans_suspend_resume_test),
	KUNIT_CASE(hci_trans_invalid_argument_test),
	KUNIT_CASE(hci_trans_del_free_test),
	{}
};

static struct kunit_suite hci_trans_test_suite = {
	.name = "hci_trans_unittest",
	.test_cases = hci_trans_test_cases,
};

kunit_test_suite(hci_trans_test_suite);
