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
#include "../hci_h4.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
static struct hci_trans *htr = NULL;
static int test_hci_h4_init(struct kunit *test)
{
	kunit_info(test, "hci_h4 init\n");
	if (htr == NULL)
		htr = hci_trans_new("h4 test");
	return 0;
}

static void test_hci_h4_exit(struct kunit *test)
{
	kunit_info(test, "hci_h4 exit\n");
	if (htr) {
		hci_trans_del(htr);
		hci_trans_free(htr);
		htr = NULL;
	}
}

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void hci_h4_init_deinit_test(struct kunit *test)
{
	kunit_info(test, "hci_h4_init_deinit test case\n");

	KUNIT_EXPECT_EQ(test, -EINVAL, hci_h4_init(NULL, false));
	KUNIT_EXPECT_EQ(test, 0, hci_h4_init(htr, false));
	KUNIT_EXPECT_EQ(test, (void *)hci_h4_skb_pack, (void *)htr->send_skb);
	hci_h4_deinit(htr);

	KUNIT_EXPECT_EQ(test, 0, hci_h4_init(htr, true));
	KUNIT_EXPECT_EQ(test, (void *)hci_h4_skb_unpack, (void *)htr->send_skb);
	hci_h4_deinit(htr);

	KUNIT_SUCCEED(test);
}

static void hci_h4_send_recv_test(struct kunit *test)
{
	char tdata[] = { HCI_COMMAND_PKT, 0x00, 0x00, 0x02, 0xFF, 0xFF };
	char *thci = tdata+1;
	unsigned int type = HCITRARG_SET(tdata[0]);

	kunit_info(test, "hci_h4_send_recv test case\n");
	hci_h4_init(htr, false);

	kunit_info(test, "send one packet\n");
	KUNIT_EXPECT_EQ(test,
			-EINVAL, // it does not have the next htr
			htr->send(htr, thci, sizeof(tdata)-1, type));

	kunit_info(test, "send seperated one packet\n");
	KUNIT_EXPECT_EQ(test, 3, htr->send(htr, thci, 3, type));
	KUNIT_EXPECT_EQ(test,
			-EINVAL, // it does not have the next htr
			htr->send(htr, thci+3, 2, type));

	kunit_info(test, "recv seperated one packet\n");
	KUNIT_EXPECT_EQ(test, 3, htr->recv(htr, tdata, 3, type));
	KUNIT_EXPECT_EQ(test,
			-EINVAL, // it does not have the prev htr
			htr->recv(htr, tdata+3, sizeof(tdata)-3, type));

	kunit_info(test, "recv overrun one packet\n");
	KUNIT_EXPECT_EQ(test,
			sizeof(tdata)+1,
			htr->recv(htr, tdata, sizeof(tdata)+1, type));
	hci_h4_deinit(htr);
	KUNIT_SUCCEED(test);
}

static void hci_h4_proc_show_test(struct kunit *test)
{
	struct file *file = kunit_kmalloc(test, sizeof(struct file), GFP_KERNEL);
	struct seq_file *s;
#define PROC_TEST_BUF_SIZE      (1024*10)
	char *buf = kunit_kmalloc(test, PROC_TEST_BUF_SIZE, GFP_USER);

	kunit_info(test, "hci_h4_proc_show test case\n");

	single_open(file, hci_h4_proc_show, NULL);

	// fake seq_file
	s = file->private_data;
	s->count = 0;
	s->size = PROC_TEST_BUF_SIZE;
	s->buf = buf;

	hci_h4_init(htr, false);
	hci_h4_proc_show(htr, s);
	hci_h4_deinit(htr);

	kunit_kfree(test, buf);
	kunit_kfree(test, file);

	KUNIT_SUCCEED(test);
}

static void hci_h4_1(struct kunit *test)
{
	kunit_info(test, "hci_h4 test case\n");
	KUNIT_SUCCEED(test);
}

static struct kunit_case hci_h4_test_cases[] = {
	KUNIT_CASE(hci_h4_init_deinit_test),
	KUNIT_CASE(hci_h4_send_recv_test),
	KUNIT_CASE(hci_h4_proc_show_test),
	KUNIT_CASE(hci_h4_1),
	{}
};

static struct kunit_suite hci_h4_test_suite = {
	.name = "hci_h4_unittest",
	.test_cases = hci_h4_test_cases,
	.init = test_hci_h4_init,
	.exit = test_hci_h4_exit,
};

kunit_test_suite(hci_h4_test_suite);
