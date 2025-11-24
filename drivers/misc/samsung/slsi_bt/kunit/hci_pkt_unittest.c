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
#include "../hci_pkt.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void hci_pkt_status_test(struct kunit *test)
{
	char tdata[] = { 0x00, 0x01, 0x02, 0x03 };

	kunit_info(test, "hci_pkt_status test case\n");
	// missing packet data
	KUNIT_EXPECT_EQ(test,
			HCI_PKT_STATUS_NO_DATA,
			hci_pkt_status(0, NULL, 1));
	KUNIT_EXPECT_EQ(test,
			HCI_PKT_STATUS_NO_DATA,
			hci_pkt_status(0, tdata, 0));
	// Unknown type
	KUNIT_EXPECT_EQ(test,
			HCI_PKT_STATUS_UNKNOWN_TYPE,
			hci_pkt_status(0, tdata, sizeof(tdata)));
	// Not enough header
	KUNIT_EXPECT_EQ(test,
			HCI_PKT_STATUS_NOT_ENOUGH_HEADER,
			hci_pkt_status(HCI_COMMAND_PKT, tdata, 2));
	// Not enough length
	KUNIT_EXPECT_EQ(test,
			HCI_PKT_STATUS_NOT_ENOUGH_LENGTH,
			hci_pkt_status(HCI_COMMAND_PKT, tdata, sizeof(tdata)));
	// overrun length
	KUNIT_EXPECT_EQ(test,
			HCI_PKT_STATUS_OVERRUN_LENGTH,
			hci_pkt_status(HCI_COMMAND_PKT, tdata, 10));
	// complete
	tdata[(int)hci_command_type.len_offset] =
				sizeof(tdata) - hci_command_type.hdr_size;
	KUNIT_EXPECT_EQ(test,
			HCI_PKT_STATUS_COMPLETE,
			hci_pkt_status(HCI_COMMAND_PKT, tdata, sizeof(tdata)));

	KUNIT_SUCCEED(test);
}

static void hci_pkt_getter_test(struct kunit *test)
{
	char tdata[5] = { 0, };
	uint16_t tcmd = HCI_VSC_FWLOG_BTSNOOP;
	char tpsize = sizeof(tdata) - hci_command_type.hdr_size;

	kunit_info(test, "hci_pkt_getter test case\n");
	// tdata set
	memcpy(tdata+0, &tcmd, sizeof(tcmd));
	tdata[(int)hci_command_type.len_offset] = tpsize;

	// get pkt size
	KUNIT_EXPECT_EQ(test, 0, hci_pkt_get_size(0, NULL, 0));
	KUNIT_EXPECT_EQ(test, 0, hci_pkt_get_size(0, tdata, sizeof(tdata)));
	KUNIT_EXPECT_EQ(test,
			0,
			hci_pkt_get_size(HCI_COMMAND_PKT, tdata, 1));
	KUNIT_EXPECT_EQ(test,
			sizeof(tdata),
			hci_pkt_get_size(HCI_COMMAND_PKT,
					 tdata,
					 sizeof(tdata)));

	// get command
	KUNIT_EXPECT_EQ(test, tcmd, hci_pkt_get_command(tdata, sizeof(tdata)));

	KUNIT_SUCCEED(test);
}

static void hci_pkt_allocate_test(struct kunit *test)
{
	struct skb_buff *skb = NULL;

	kunit_info(test, "hci_pkt_allocate test case\n");
	KUNIT_EXPECT_NOT_NULL(test, skb = alloc_hci_pkt_skb(10));

	KUNIT_SUCCEED(test);
}

static struct kunit_case hci_pkt_test_cases[] = {
	KUNIT_CASE(hci_pkt_status_test),
	KUNIT_CASE(hci_pkt_getter_test),
	KUNIT_CASE(hci_pkt_allocate_test),
	{}
};

static struct kunit_suite hci_pkt_test_suite = {
	.name = "hci_pkt_unittest",
	.test_cases = hci_pkt_test_cases,
};

kunit_test_suite(hci_pkt_test_suite);
