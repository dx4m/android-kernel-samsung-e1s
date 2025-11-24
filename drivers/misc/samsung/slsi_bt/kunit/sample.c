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

/******************************************************************************
 * Common test code
 ******************************************************************************/
static int test_sample_init(struct kunit *test)
{
	kunit_info(test, "sample init\n");
	return 0;
}

static void test_sample_exit(struct kunit *test)
{
	kunit_info(test, "sample exit\n");
}

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void sample_1(struct kunit *test)
{
	kunit_info(test, "sample test case\n");
	KUNIT_SUCCEED(test);
}

static struct kunit_case sample_test_cases[] = {
	KUNIT_CASE(sample_1),
	{}
};

static struct kunit_suite sample_test_suite = {
	.name = "sample_unittest",
	.test_cases = sample_test_cases,
	.init = test_sample_init,
	.exit = test_sample_exit,
};

kunit_test_suite(sample_test_suite);
