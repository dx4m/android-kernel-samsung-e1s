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
#include <linux/of.h>
#define of_property_read_u32(np, name, value)    (true)

/******************************************************************************
 * Test target code
 ******************************************************************************/
#include "../slsi_bt_qos.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
static struct platform_device fake_dev;
static bool stay_init_status = false;
static int test_slsi_bt_qos_init(struct kunit *test)
{
	kunit_info(test, "slsi_bt_qos init\n");
	if (qos_pdev == NULL) {
		slsi_bt_qos_service_init();
		platform_bt_qos_driver.probe(&fake_dev);
		stay_init_status = false;
	} else
		stay_init_status = true;
	
	return 0;
}

static void test_slsi_bt_qos_exit(struct kunit *test)
{
	kunit_info(test, "slsi_bt_qos exit\n");
	if (qos_pdev) {
		slsi_bt_qos_service_exit();
		if (stay_init_status)
			slsi_bt_qos_service_init();
	}
}

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void slsi_bt_qos_reset_test(struct kunit *test)
{
	kunit_info(test, "slsi_bt_qos_reset test case\n");

	slsi_bt_qos_service_exit();
	slsi_bt_qos_reset_ops.set(0, NULL);

	slsi_bt_qos_service_init();
	platform_bt_qos_driver.probe(&fake_dev);
	slsi_bt_qos_reset_ops.set(0, NULL);

	slsi_bt_qos_level_reset(&fake_dev);

	KUNIT_SUCCEED(test);
}

static void slsi_bt_qos_start_test(struct kunit *test)
{
	int fake;
	struct slsi_bt_qos *qos;

	kunit_info(test, "slsi_bt_qos_start test case\n");

	// start failed
	qos = slsi_bt_qos_start(NULL);
	KUNIT_EXPECT_EQ(test, NULL, qos);

	// start
	qos = slsi_bt_qos_start(&fake);
	KUNIT_EXPECT_NOT_NULL(test, qos);

	// reset
	slsi_bt_qos_update(qos, 0);
	msleep(10);
	// high T put
	slsi_bt_qos_update(qos, 100000);
	slsi_bt_qos_update(qos, 100000);
	msleep(100);
	// apply high T put
	slsi_bt_qos_update(qos, 100000);

	msleep(100);
	// low T put
	slsi_bt_qos_update(qos, 10);

	// timeout to disable
	msleep(1000);

	// stop failed
	slsi_bt_qos_stop(NULL);
	// stop
	slsi_bt_qos_stop(qos);

	KUNIT_SUCCEED(test);
}

static void slsi_bt_qos_1(struct kunit *test)
{
	kunit_info(test, "slsi_bt_qos test case\n");
	KUNIT_SUCCEED(test);
}

static struct kunit_case slsi_bt_qos_test_cases[] = {
	KUNIT_CASE(slsi_bt_qos_reset_test),
	KUNIT_CASE(slsi_bt_qos_start_test),
	KUNIT_CASE(slsi_bt_qos_1),
	{}
};

static struct kunit_suite slsi_bt_qos_test_suite = {
	.name = "slsi_bt_qos_unittest",
	.test_cases = slsi_bt_qos_test_cases,
	.init = test_slsi_bt_qos_init,
	.exit = test_slsi_bt_qos_exit,
};

kunit_test_suite(slsi_bt_qos_test_suite);
