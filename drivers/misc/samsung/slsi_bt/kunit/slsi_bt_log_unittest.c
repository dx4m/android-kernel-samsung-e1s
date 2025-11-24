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
#include "fake.h"
#include "../../scsc/mxlog_transport.h"
static struct mxlog_transport *scsc_mx_get_mxlog_transport_wpan_ret = NULL;
struct mxlog_transport *scsc_mx_get_mxlog_transport_wpan(struct scsc_mx *mx)
{
	return scsc_mx_get_mxlog_transport_wpan_ret;
}

/******************************************************************************
 * Test target code
 ******************************************************************************/
#include "../slsi_bt_log.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void slsi_bt_log_firm_log_filter_test(struct kunit *test)
{
	char tdata[]        = "+0x0123456789ABCDEF0123456789ABCDEF\n"; // 32 hex
	char tdata_max[]    = "+0x0123456789ABCDEF0123456789ABCDEF00"; // 34 hex
	char tdata_inval1[] = "+0x0123456789ABCDEF00000INVAL";
	char tdata_inval2[] = "+0x0123456789ABCDEF00000INVAL";
	struct firm_log_filter result;
	char buf[256];

	kunit_info(test, "slsi_bt_log_firm_log_filter test case\n");
	// mxlog_filter_set
	KUNIT_EXPECT_EQ(test, 0, mxlog_filter_set("1234", NULL));
	KUNIT_EXPECT_EQ(test, 1234, fw_filter.uint32[0]);

	KUNIT_EXPECT_EQ(test, 0, mxlog_filter_set("0xABCD", NULL));
	KUNIT_EXPECT_EQ(test, 0xABCD, fw_filter.uint32[0]);

	KUNIT_EXPECT_EQ(test, -EINVAL, mxlog_filter_set("ABCD", NULL));

	// mxlog_filter_get
	KUNIT_EXPECT_TRUE(test, mxlog_filter_get(buf, NULL));
	KUNIT_EXPECT_EQ_MSG(test,
			    strlen(buf), 24,
			    "mxlog_filter=0x%08x\n", buf);

	// get fw_filter value
	result = slsi_bt_log_filter_get();
	KUNIT_EXPECT_TRUE(test,
			  !memcmp(&fw_filter, &result, sizeof(fw_filter)));

	// btlog_reset
	KUNIT_EXPECT_EQ(test, 0, btlog_reset(NULL, NULL));
	KUNIT_EXPECT_TRUE(test,
		  !memcmp(&fw_filter, &fw_filter_default, sizeof(fw_filter)));

	// btlog_enables_set
	KUNIT_EXPECT_EQ(test, -EINVAL, btlog_enables_set(NULL, NULL));
	// no hex prefix
	KUNIT_EXPECT_EQ(test, -EINVAL, btlog_enables_set("1234", NULL));
	// over range
	KUNIT_EXPECT_EQ(test, -ERANGE, btlog_enables_set(tdata_max, NULL));
	// unexpected hex
	KUNIT_EXPECT_EQ(test, -EINVAL, btlog_enables_set("0xNOHEX", NULL));
	// invalid data
	KUNIT_EXPECT_EQ(test, -EINVAL, btlog_enables_set(tdata_inval1, NULL));
	KUNIT_EXPECT_EQ(test, -EINVAL, btlog_enables_set(tdata_inval2, NULL));

	KUNIT_EXPECT_EQ(test, 0, btlog_enables_set(tdata, NULL));
	KUNIT_EXPECT_EQ(test, 0, btlog_enables_set("0xABCD", NULL));
	KUNIT_EXPECT_EQ(test, 0xABCD, fw_filter.uint32[0]);

	// btlog_enables_get
	KUNIT_EXPECT_TRUE(test, btlog_enables_get(buf, NULL));
	KUNIT_EXPECT_EQ_MSG(test,
			    strlen(buf), 51,
			    "btlog_enables = 0x%08x%08x%08x%08x\n", buf);
	KUNIT_SUCCEED(test);
}

static void slsi_bt_log_set_transport_test(struct kunit *test)
{
	int fake;

	kunit_info(test, "slsi_bt_log_set_transport test case\n");
	slsi_bt_log_set_transport(&fake);
	slsi_bt_log_set_transport(NULL);
	KUNIT_SUCCEED(test);
}

static void slsi_bt_log_trace_hex_data_test(struct kunit *test)
{
	char tdata[] = { 0x01, 0x02, 0x03, 0x04 };
	char tdata2[1024] = { 0x00, };

	kunit_info(test, "slsi_bt_log_trace_hex_data test case\n");
	slsi_bt_log_data_hex("fn", BIT(6), tdata, sizeof(tdata));
	slsi_bt_log_data_hex("fn", BTTR_TAG_BCSP_TX, tdata, sizeof(tdata));
	slsi_bt_log_data_hex("fn", BTTR_TAG_BCSP_TX, tdata2, sizeof(tdata2));
	KUNIT_SUCCEED(test);
}

static void slsi_bt_log_mxlog_log_event_test(struct kunit *test)
{
	struct mxlog_transport fake_mtrans = {
		.header_handler_fn = NULL,
		.channel_handler_fn = NULL,
	};
	char tdata[10] = { 0x00, };

	kunit_info(test, "slsi_bt_log_mxlog_log_event test case\n");
	slsi_bt_mxlog_log_event(0, NULL);

	scsc_mx_get_mxlog_transport_wpan_ret = (void *)&fake_mtrans;
	slsi_bt_mxlog_log_event(sizeof(tdata), tdata);
	KUNIT_SUCCEED(test);
}

static void slsi_bt_log_fwsnoop_test(struct kunit *test)
{
	struct sk_buff_head tq;
	struct hci_trans *htr = hci_trans_new("fwsnoop_dump_test");
	char tdata[] = { 0x01, 0x02, 0x03, 0x04 };

	// setup
	slsi_bt_log_set_transport(htr);
	skb_queue_head_init(&tq);

	kunit_info(test, "slsi_bt_log_fwsnoop test case\n");
	// enable without btlog_reset
	slsi_bt_fwlog_snoop_enable(fw_filter_default.uint32);
	slsi_bt_fwlog_snoop_disable();

	// enable after reset
	btlog_reset(NULL, NULL);
	slsi_bt_fwlog_snoop_enable(fw_filter_default.uint32);

	// push dump
	for (int i = 0; i < 512; i++)
		slsi_bt_fwlog_dump(tdata, sizeof(tdata));

	// get snoop log(fail)
	slsi_bt_fwlog_snoop_queue_tail(NULL, false);
	// get snoop log
	slsi_bt_fwlog_snoop_queue_tail(&tq, true);

	slsi_bt_fwlog_dump(tdata, sizeof(tdata));
	slsi_bt_fwlog_snoop_queue_tail(&tq, false);

	// disable
	slsi_bt_fwlog_snoop_disable();

	skb_queue_purge(&tq);
	slsi_bt_log_set_transport(NULL);
	hci_trans_free(htr);
	KUNIT_SUCCEED(test);
}

static void slsi_bt_log_1(struct kunit *test)
{
	kunit_info(test, "slsi_bt_log test case\n");
	KUNIT_SUCCEED(test);
}

static struct kunit_case slsi_bt_log_test_cases[] = {
	KUNIT_CASE(slsi_bt_log_firm_log_filter_test),
	KUNIT_CASE(slsi_bt_log_set_transport_test),
	KUNIT_CASE(slsi_bt_log_trace_hex_data_test),
	KUNIT_CASE(slsi_bt_log_mxlog_log_event_test),
	KUNIT_CASE(slsi_bt_log_fwsnoop_test),
	KUNIT_CASE(slsi_bt_log_1),
	{}
};

static struct kunit_suite slsi_bt_log_test_suite = {
	.name = "slsi_bt_log_unittest",
	.test_cases = slsi_bt_log_test_cases,
};

kunit_test_suite(slsi_bt_log_test_suite);
