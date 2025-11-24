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
#define copy_to_user(d, s, n) (memcpy(d, s, n), 0)


/******************************************************************************
 * Test target code
 ******************************************************************************/
#include "../slsi_bt_io.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
extern void test_slsi_bt_controller_probe(void);
extern void test_slsi_bt_controller_remove(void);

#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
extern int test_hci_slsi_init(struct kunit *test);
extern void test_hci_slsi_exit(struct kunit *test);
#endif
int test_slsi_bt_io_init(struct kunit *test)
{
	kunit_info(test, "slsi_bt_io init bt_drv.device: %d\n",
			bt_drv.device ? true : false);
	if (!bt_drv.device) {
		slsi_bt_module_init();
		test_slsi_bt_controller_probe();
	}
#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	KUNIT_EXPECT_EQ(test, 0, test_hci_slsi_init(test));
#endif
	return 0;
}

void test_slsi_bt_io_exit(struct kunit *test)
{
	kunit_info(test, "slsi_bt_io exit\n");
	if (bt_drv.device) {
		test_slsi_bt_controller_remove();
		slsi_bt_module_exit();
	}
#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	test_hci_slsi_exit(test);
#endif
}

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void slsi_bt_io_open_close_test(struct kunit *test)
{
	kunit_info(test, "slsi_bt_io_open_close\n");

	KUNIT_EXPECT_FALSE(test, is_open_release_status());

	kunit_info(test, "Multiple open test\n");
	KUNIT_EXPECT_EQ(test, slsi_bt_h4_file_open(NULL, NULL), 0);
	bt_drv.open_status = false;
	KUNIT_EXPECT_EQ(test, slsi_bt_h4_file_open(NULL, NULL), 0);
	bt_drv.open_status = true;

	KUNIT_EXPECT_EQ(test, slsi_bt_h4_file_release(NULL, NULL), 0);
	KUNIT_EXPECT_EQ(test, slsi_bt_h4_file_release(NULL, NULL), 0);

	kunit_info(test, "Open test when it's busy\n");
	// first open
	KUNIT_EXPECT_EQ(test, slsi_bt_h4_file_open(NULL, NULL), 0);
	// try to second open
	KUNIT_EXPECT_EQ(test, slsi_bt_h4_file_open(NULL, NULL), -EBUSY);
	// try to lower open function
	KUNIT_EXPECT_EQ(test, slsi_bt_open(SLSI_BT_TR_TEST, NULL), -EBUSY);
	// release
	KUNIT_EXPECT_EQ(test, slsi_bt_h4_file_release(NULL, NULL), 0);

	KUNIT_SUCCEED(test);
}

static void slsi_bt_io_transport_init_test(struct kunit *test)
{
	kunit_info(test, "slsi_bt_io_transport_init test case\n");
	KUNIT_EXPECT_EQ(test, slsi_bt_transport_change(SLSI_BT_TR_TEST), 0);
	// same trs
	KUNIT_EXPECT_EQ(test, slsi_bt_transport_change(SLSI_BT_TR_TEST), 0);
	// deinit by trs=0
	KUNIT_EXPECT_EQ(test, slsi_bt_transport_change(0), 0);

#if IS_ENABLED(CONFIG_SLSI_BT_H4)
	KUNIT_EXPECT_EQ(test, slsi_bt_transport_change(SLSI_BT_TR_EN_H4), 0);
#endif
	KUNIT_EXPECT_EQ(test, slsi_bt_transport_change(SLSI_BT_TR_EN_PROP), 0);
#if IS_ENABLED(CONFIG_SLSI_BT_BCSP)
	KUNIT_EXPECT_EQ(test, slsi_bt_transport_change(SLSI_BT_TR_EN_BCSP), 0);
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_USE_UART_INTERFACE)
	KUNIT_EXPECT_EQ(test, slsi_bt_transport_change(SLSI_BT_TR_EN_TTY), 0);
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	KUNIT_EXPECT_EQ(test, slsi_bt_transport_change(SLSI_BT_TR_EN_HCI_UART), 0);
#endif
	slsi_bt_transport_deinit();

	KUNIT_SUCCEED(test);
}

static void slsi_bt_io_error_handle_test(struct kunit *test)
{
	kunit_info(test, "slsi_bt_io_error_handle_test\n");

	// error handle without open
	slsi_bt_error_handle(0, false);
	slsi_bt_error_packet_ready();

	// error handle after open
	slsi_bt_h4_file_open(NULL, NULL);
	slsi_bt_error_handle(0, false);
	// request restarting
	slsi_bt_error_handle(0, true);
	// twice
	slsi_bt_error_handle(0, true);

	slsi_bt_h4_file_release(NULL, NULL);
	slsi_bt_err_reset();

	KUNIT_SUCCEED(test);
}

static void slsi_bt_io_write_test(struct kunit *test)
{
	char tdata[] = {0, 1, 2, 3};
	ssize_t ret;

	kunit_info(test, "slsi_bt_io_write test case\n");
	// write data without open: Impossible case but to test htr error
	KUNIT_EXPECT_EQ(test,
			-EFAULT,
			slsi_bt_write(NULL, tdata, sizeof(tdata), NULL));

	// write data after open
	slsi_bt_h4_file_open(NULL, NULL);
	ret = slsi_bt_write(NULL, tdata, sizeof(tdata), NULL);
	kunit_info(test, "ret = %d\n", ret);
	KUNIT_EXPECT_TRUE(test,
			  ret == sizeof(tdata) || // othre transport enabled
			  ret == 0); // no htr
			
	slsi_bt_h4_file_release(NULL, NULL);
	KUNIT_SUCCEED(test);
}

static void slsi_bt_io_receive_read_test(struct kunit *test)
{
	char tdata[] = {0, 1, 2, 3};
	struct sk_buff *skb;

	kunit_info(test, "slsi_bt_io_read test case\n");

	// read data without open: Impossible case but to test htr error
	KUNIT_EXPECT_EQ(test,
			slsi_bt_read(NULL, tdata, sizeof(tdata), NULL),
			0);

	kunit_info(test, "receive one time, read one time\n");
	// receive
	slsi_bt_h4_file_open(NULL, NULL);
	skb = alloc_skb(sizeof(tdata), GFP_ATOMIC);
	skb_put_data(skb, tdata, sizeof(tdata));
	KUNIT_EXPECT_EQ(test,
			slsi_bt_skb_recv(bt_trans.htr, skb),
			sizeof(tdata));
	// poll
	KUNIT_EXPECT_EQ(test,
			slsi_bt_poll(NULL, NULL),
			POLLIN | POLLRDNORM);
	// read
	KUNIT_EXPECT_EQ(test,
			slsi_bt_read(NULL, tdata, sizeof(tdata), NULL),
			sizeof(tdata));
	// empty poll
	KUNIT_EXPECT_EQ(test, slsi_bt_poll(NULL, NULL), POLLOUT);
	slsi_bt_h4_file_release(NULL, NULL);

	kunit_info(test, "receive one time, read multiple times\n");
	// receive
	slsi_bt_h4_file_open(NULL, NULL);
	skb = alloc_skb(sizeof(tdata), GFP_ATOMIC);
	skb_put_data(skb, tdata, sizeof(tdata));
	KUNIT_EXPECT_EQ(test,
			slsi_bt_skb_recv(bt_trans.htr, skb),
			sizeof(tdata));
	for (int i = 0; i < sizeof(tdata); i++) {
		// poll
		KUNIT_EXPECT_EQ(test,
				slsi_bt_poll(NULL, NULL),
				POLLIN | POLLRDNORM);
		// read
		KUNIT_EXPECT_EQ(test,
				slsi_bt_read(NULL, tdata, 1, NULL),
				1);
	}
	// empty poll
	KUNIT_EXPECT_EQ(test, slsi_bt_poll(NULL, NULL), POLLOUT);
	
	slsi_bt_h4_file_release(NULL, NULL);

	KUNIT_SUCCEED(test);
}

static void slsi_bt_io_ioctl_test(struct kunit *test)
{
	kunit_info(test, "slsi_bt_io_ioctl test case\n");
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_ioctl(NULL, SBTIOCT_CHANGE_TRS, 0));
	KUNIT_SUCCEED(test);
}

static void slsi_bt_io_proc_test(struct kunit *test)
{
	struct file *file = kunit_kmalloc(test, sizeof(struct file), GFP_KERNEL);
	struct seq_file *s;
#define PROC_TEST_BUF_SIZE      (1024*20)
	char *buf = kunit_kmalloc(test, PROC_TEST_BUF_SIZE, GFP_USER);

	kunit_info(test, "slsi_bt_io_proc test case\n");

	slsi_bt_h4_file_open(NULL, NULL);
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_service_proc_open(NULL, file));

	// fake seq_file
	s = file->private_data;
	s->count = 0;
	s->size = PROC_TEST_BUF_SIZE;
	s->buf = buf;

	KUNIT_EXPECT_EQ(test, 0, slsi_bt_service_proc_show(s, NULL));
	slsi_bt_h4_file_release(NULL, NULL);

	kunit_kfree(test, buf);
	kunit_kfree(test, file);

	KUNIT_SUCCEED(test);
}

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
struct test_helper_lr_wpan_st {
	struct file_operations *fops;
	ssize_t (*wait_for_data)(void);
	struct hci_trans *htr;
} test_helper_lr_wpan = {
	.fops = &slsi_lr_wpan_fops,
	.wait_for_data = slsi_lr_wpan_wait_for_data,
};

void *test_helper_get_lr_wpan_context(void)
{
	test_helper_lr_wpan.htr = bt_trans.lr_wpan_htr;
	return &test_helper_lr_wpan;
}
#endif

static struct kunit_case slsi_bt_io_test_cases[] = {
	KUNIT_CASE(slsi_bt_io_open_close_test),
	KUNIT_CASE(slsi_bt_io_transport_init_test),
	KUNIT_CASE(slsi_bt_io_error_handle_test),
	KUNIT_CASE(slsi_bt_io_write_test),
	KUNIT_CASE(slsi_bt_io_receive_read_test),
	KUNIT_CASE(slsi_bt_io_ioctl_test),
	KUNIT_CASE(slsi_bt_io_proc_test),
	{}
};

static struct kunit_suite slsi_bt_io_test_suite = {
	.name = "slsi_bt_io_unittest",
	.test_cases = slsi_bt_io_test_cases,
	.init = test_slsi_bt_io_init,
	.exit = test_slsi_bt_io_exit,
};

kunit_test_suite(slsi_bt_io_test_suite);
