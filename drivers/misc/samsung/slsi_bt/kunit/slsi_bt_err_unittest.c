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
#include "../slsi_bt_err.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
static void err_callback(int reason, bool lazy)
{
}

static int test_slsi_bt_err_init(struct kunit *test)
{
	kunit_info(test, "slsi_bt_err init\n");
	if (wq == NULL)
		slsi_bt_err_init(err_callback);
	return 0;
}

static void test_slsi_bt_err_exit(struct kunit *test)
{
	kunit_info(test, "slsi_bt_err exit\n");
	if (wq)
		slsi_bt_err_deinit();
}

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void slsi_bt_err_test(struct kunit *test)
{
	kunit_info(test, "slsi_bt_err test case\n");
	// normal error
	slsi_bt_err(0);
	KUNIT_EXPECT_TRUE(test, slsi_bt_err_status());
	KUNIT_EXPECT_FALSE(test, slsi_bt_in_recovery_progress());

	// reset
	slsi_bt_err_reset();
	KUNIT_EXPECT_FALSE(test, slsi_bt_err_status());

	// recovery error
	slsi_bt_err(SLSI_BT_ERR_MX_FAIL);
	KUNIT_EXPECT_EQ(test, 1, slsi_bt_err_status());
	KUNIT_EXPECT_TRUE(test, slsi_bt_in_recovery_progress());
	slsi_bt_err(SLSI_BT_ERR_MX_RESET);
	KUNIT_EXPECT_EQ(test, 2, slsi_bt_err_status());
	KUNIT_EXPECT_FALSE(test, slsi_bt_in_recovery_progress());

	// reset in the recovery status
	slsi_bt_err(SLSI_BT_ERR_MX_FAIL);
	KUNIT_EXPECT_TRUE(test, slsi_bt_in_recovery_progress());
	slsi_bt_err_reset();
	KUNIT_EXPECT_FALSE(test, slsi_bt_err_status());

	// many error
	slsi_bt_err(SLSI_BT_ERR_MX_FAIL);
	slsi_bt_err(SLSI_BT_ERR_MX_RESET);
	for (int i = 0; i < SLSI_BT_ERR_HISTORY_SIZE; i++)
		slsi_bt_err(0);
	KUNIT_EXPECT_EQ(test, SLSI_BT_ERR_HISTORY_SIZE+2, slsi_bt_err_status());
	slsi_bt_err_reset();

	KUNIT_SUCCEED(test);
}

static void slsi_bt_err_force_error_test(struct kunit *test)
{
	kunit_info(test, "slsi_bt_err test case\n");

	slsi_bt_err_reset();
	KUNIT_EXPECT_FALSE(test, slsi_bt_err_status());

	KUNIT_EXPECT_EQ(test, 0, force_crash_set("0x12341234", NULL));
	KUNIT_EXPECT_FALSE(test, slsi_bt_err_status());

	KUNIT_EXPECT_EQ(test, 0, force_crash_set("0xDEADDEAD", NULL));
	KUNIT_EXPECT_TRUE(test, slsi_bt_err_status());
	KUNIT_SUCCEED(test);
}

static void slsi_bt_err_proc_show_test(struct kunit *test)
{
	struct file *file = kunit_kmalloc(test, sizeof(struct file), GFP_KERNEL);
	struct seq_file *s;
#define PROC_TEST_BUF_SIZE      (1024*10)
	char *buf = kunit_kmalloc(test, PROC_TEST_BUF_SIZE, GFP_USER);

	kunit_info(test, "slsi_bt_err_proc_show test case\n");

	single_open(file, slsi_bt_err_proc_show, NULL);

	// fake seq_file
	s = file->private_data;
	s->count = 0;
	s->size = PROC_TEST_BUF_SIZE;
	s->buf = buf;

	slsi_bt_err_proc_show(s, NULL);

	kunit_kfree(test, buf);
	kunit_kfree(test, file);

	KUNIT_SUCCEED(test);
}


static struct kunit_case slsi_bt_err_test_cases[] = {
	KUNIT_CASE(slsi_bt_err_test),
	KUNIT_CASE(slsi_bt_err_force_error_test),
	KUNIT_CASE(slsi_bt_err_proc_show_test),
	{}
};

static struct kunit_suite slsi_bt_err_test_suite = {
	.name = "slsi_bt_err_unittest",
	.test_cases = slsi_bt_err_test_cases,
	.init = test_slsi_bt_err_init,
	.exit = test_slsi_bt_err_exit,
};

kunit_test_suite(slsi_bt_err_test_suite);
