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
#include <linux/device.h>
struct hci_slsidev fake_hci_slsidev;
#define devm_kzalloc(dev, size, gfp)   (&fake_hci_slsidev)
#define devm_kfree(dev, p)

#include <linux/serdev.h>
#define serdev_device_open(dev)                 (0)
#define serdev_device_close(dev)
#define serdev_device_write_buf(dev, data, len) (len > 9 ? len / 2 : len)

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <net/bluetooth/hci_core.h>
#define hci_register_dev(hdev)                  (0)
#define hci_unregister_dev(hdev)



/******************************************************************************
 * Test target code
 ******************************************************************************/
#include "../hci_slsi.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
struct serdev_device fake_serdev;
int test_hci_slsi_init(struct kunit *test)
{
	kunit_info(test, "hci_slsi init\n");
	if (!hci_slsidev) {
		return slsi_serdev_driver.probe(&fake_serdev);
	}
	return 0;
}

void test_hci_slsi_exit(struct kunit *test)
{
	kunit_info(test, "hci_slsi exit\n");
	if (hci_slsidev) {
		slsi_serdev_driver.remove(&fake_serdev);
	}
}

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void _hci_slsi_open_test(struct kunit *test)
{
	struct hci_dev *hdev = hci_slsidev->hdev;

	kunit_info(test, "hci_slsi_open\n");

	KUNIT_EXPECT_EQ(test, 0, hci_slsi_open(hdev));
	// hci_slsi returns -EAGAIN at the first call
	KUNIT_EXPECT_EQ(test, -EAGAIN, hci_slsi_setup(hdev));

	// hci_slsi returns 0 after calling the diagnostic function
	KUNIT_EXPECT_EQ(test, 0, hci_slsi_setup_diag(hdev, true));
	KUNIT_EXPECT_EQ(test, 0, hci_slsi_setup(hdev));

	KUNIT_SUCCEED(test);
}

static void _hci_slsi_close_test(struct kunit *test)
{
	struct hci_dev *hdev = hci_slsidev->hdev;

	kunit_info(test, "hci_slsi_close\n");
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_hdev_shutdown(hdev));
	KUNIT_EXPECT_EQ(test, 0, hci_slsi_close(hdev));

	KUNIT_SUCCEED(test);
}

static void hci_slsi_open_close_test(struct kunit *test)
{
	kunit_info(test, "hci_slsi_open_close test case\n");

	_hci_slsi_open_test(test);
	_hci_slsi_close_test(test);

	KUNIT_SUCCEED(test);
}

static void hci_slsi_send_test(struct kunit *test)
{
	struct hci_dev *hdev = hci_slsidev->hdev;
	struct hci_slsidev *hsdev = hci_get_drvdata(hdev);
	char tdata[10];	 // tester will send twiece if it is bigger than 10 bytes
	struct sk_buff *skb;

	kunit_info(test, "hci_slsi_send test case\n");

	_hci_slsi_open_test(test);

	// setup reopen with only HCI_UART transport layer
	slsi_bt_release();
	_slsi_bt_setup(hsdev, SLSI_BT_TR_EN_HCI_UART);

	skb = alloc_skb(sizeof(tdata), GFP_ATOMIC);
	skb_put_data(skb, tdata, sizeof(tdata));
	hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;
	KUNIT_EXPECT_EQ(test, 0, hdev->send(hdev, skb));

	// twice
	skb = alloc_skb(sizeof(tdata), GFP_ATOMIC);
	skb_put_data(skb, tdata, sizeof(tdata));
	hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;
	KUNIT_EXPECT_EQ(test, 0, hdev->send(hdev, skb));

	msleep(10);

	KUNIT_EXPECT_EQ(test, 2, hci_slsidev->hdev->stat.cmd_tx);

	// block to send
	hci_slsi_block(hci_slsidev->htr);
	KUNIT_EXPECT_TRUE(test,
			test_bit(HCI_SLSIDEV_STOP_SEND_FRAME, &hsdev->flags));
	skb = alloc_skb(4, GFP_ATOMIC);
	skb_put_data(skb, tdata, 4);
	hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;
	KUNIT_EXPECT_EQ(test, 0, hdev->send(hdev, skb));

	KUNIT_EXPECT_EQ(test, 2, hci_slsidev->hdev->stat.cmd_tx);

	// resume
	hci_slsi_resume(hci_slsidev->htr);
	KUNIT_EXPECT_FALSE(test,
			test_bit(HCI_SLSIDEV_STOP_SEND_FRAME, &hsdev->flags));
	skb = alloc_skb(4, GFP_ATOMIC);
	skb_put_data(skb, tdata, 4);
	hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;
	KUNIT_EXPECT_EQ(test, 0, hdev->send(hdev, skb));

	KUNIT_EXPECT_EQ(test, 3, hci_slsidev->hdev->stat.cmd_tx);

	_hci_slsi_close_test(test);

	KUNIT_SUCCEED(test);
}

static void hci_slsi_recv_test(struct kunit *test)
{
	struct hci_dev *hdev = hci_slsidev->hdev;
	struct hci_slsidev *hsdev = hci_get_drvdata(hdev);
	char tdata[10];	 // tester will send twiece if it is bigger than 10 bytes

	kunit_info(test, "hci_slsi test case\n");
	_hci_slsi_open_test(test);

	// setup reopen with only HCI_UART transport layer
	slsi_bt_release();
	_slsi_bt_setup(hsdev, SLSI_BT_TR_EN_HCI_UART);
	hsdev->top_htr->tdata = hsdev;
	hsdev->top_htr->recv_skb = hci_slsi_recv_skb;

	slsi_bt_receive_buf(&fake_serdev, tdata, sizeof(tdata));

	_hci_slsi_close_test(test);
	KUNIT_SUCCEED(test);
}

static struct kunit_case hci_slsi_test_cases[] = {
	KUNIT_CASE(hci_slsi_open_close_test),
	KUNIT_CASE(hci_slsi_send_test),
	KUNIT_CASE(hci_slsi_recv_test),
	{}
};

static struct kunit_suite hci_slsi_test_suite = {
	.name = "hci_slsi_unittest",
	.test_cases = hci_slsi_test_cases,
	.init = test_hci_slsi_init,
	.exit = test_hci_slsi_exit,
};

kunit_test_suite(hci_slsi_test_suite);
