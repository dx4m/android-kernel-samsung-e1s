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
#include <linux/interrupt.h>
#define devm_request_threaded_irq(dev, irq, handler, fn, flags, name, id) (0)
#define devm_free_irq(dev, irq, id)

#include <linux/of_gpio.h>
#define of_get_named_gpio(np, name, index) (1)

#include <linux/gpio.h>
#define gpio_is_valid(num)  (num)
#define gpio_to_irq(num)    (num)
#define gpio_get_value(num) (num)
#define enable_irq_wake(irq)
#define disable_irq_wake(irq)


/******************************************************************************
 * Test target code
 ******************************************************************************/
#include "../slsi_bt_controller.c"

/******************************************************************************
 * Common test code
 ******************************************************************************/
static struct platform_device fake_pdev;
void test_slsi_bt_controller_probe(void)
{
	if (common_srv.wq && common_srv.is_platform_driver)
		platform_bt_controller_driver.probe(&fake_pdev);
}

void test_slsi_bt_controller_remove(void)
{
	if (common_srv.wq == NULL)
		platform_bt_controller_driver.remove(&fake_pdev);
}

int test_slsi_bt_controller_init(struct kunit *test)
{
	kunit_info(test, "slsi_bt_controller init\n");
	if (common_srv.wq == NULL)
		slsi_bt_controller_init();
	test_slsi_bt_controller_probe();

	return 0;
}

void test_slsi_bt_controller_exit(struct kunit *test)
{
	kunit_info(test, "slsi_bt_controller exit\n");
	test_slsi_bt_controller_remove();
	if (common_srv.wq)
		slsi_bt_controller_exit();
}

static int loopback_tester(struct hci_trans *htr, struct sk_buff *skb)
{
	struct hci_trans *prev = hci_trans_get_prev(htr);
	int ret = skb->len;

	prev->recv(prev, skb->data, skb->len, 0);
	kfree_skb(skb);

	return ret;
}

/******************************************************************************
 * Test cases
 ******************************************************************************/
static void slsi_bt_controller_remove_probe_in_recovery_test(struct kunit *test)
{
	struct scsc_mx *mx = common_srv.mx;

	kunit_info(test, "remove_probe in recover test case\n");

	bt_module_client.remove(&bt_module_client, mx,
				SCSC_MODULE_CLIENT_REASON_RECOVERY_WPAN);
	bt_module_client.probe(&bt_module_client, mx,
				SCSC_MODULE_CLIENT_REASON_RECOVERY_WPAN);
	KUNIT_SUCCEED(test);
}

static void slsi_bt_controller_start_stop_test(struct kunit *test)
{
	kunit_info(test, "slsi_bt_controller_start_stop test case\n");

	kunit_info(test, "Test SERVICE_ID_BT\n");
	// don't use worker
	disable_worker_for_mx = true;
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_start(SCSC_SERVICE_ID_BT));
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_stop(SCSC_SERVICE_ID_BT));
	// use worker
	disable_worker_for_mx = false;
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_start(SCSC_SERVICE_ID_BT));
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_stop(SCSC_SERVICE_ID_BT));

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	kunit_info(test, "Test SCSC_SERVICE_ID_LR_WPAN\n");
	KUNIT_EXPECT_EQ(test,
			0,
			slsi_bt_controller_start(SCSC_SERVICE_ID_LR_WPAN));
	KUNIT_EXPECT_EQ(test,
			0,
			slsi_bt_controller_stop(SCSC_SERVICE_ID_LR_WPAN));

	kunit_info(test, "Start both of service\n");
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_start(SCSC_SERVICE_ID_BT));
	KUNIT_EXPECT_EQ(test,
			0,
			slsi_bt_controller_start(SCSC_SERVICE_ID_LR_WPAN));
	KUNIT_EXPECT_EQ(test,
			0,
			slsi_bt_controller_stop(SCSC_SERVICE_ID_LR_WPAN));
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_stop(SCSC_SERVICE_ID_BT));
#endif

	kunit_info(test, "Test unknown service id\n");
	KUNIT_EXPECT_EQ(test, -EFAULT, slsi_bt_controller_start(0));
	KUNIT_EXPECT_EQ(test, -EFAULT, slsi_bt_controller_stop(0));

	KUNIT_SUCCEED(test);
}

static void slsi_bt_controller_pm_notifier_test(struct kunit *test)
{
	int fake;
	struct mx_syserr_decode terr = { .level = 4 };

	kunit_info(test, "slsi_bt_controller_notifier test case\n");
	// suspend
	KUNIT_EXPECT_EQ(test,
			NOTIFY_OK,
			slsi_bt_controller_nb.notifier_call(
					&slsi_bt_controller_nb,
					PM_SUSPEND_PREPARE,
					NULL)
			);
	common_srv.wake_src = 1;
	common_srv.wake_irq = 1;
	KUNIT_EXPECT_EQ(test,
			-EBUSY,
			platform_bt_controller_pm_ops.suspend(NULL));
	common_srv.wake_src = 0;
	common_srv.wake_irq = 0;
	KUNIT_EXPECT_EQ(test, 0, platform_bt_controller_pm_ops.suspend(NULL));

	// resume
	KUNIT_EXPECT_EQ(test,
			NOTIFY_OK,
			slsi_bt_controller_nb.notifier_call(
					&slsi_bt_controller_nb,
					PM_POST_SUSPEND,
					NULL)
			);

	// interfupt
	fake = 1;
	KUNIT_EXPECT_EQ(test,
			IRQ_HANDLED,
			slsi_bt_controller_wakeup_threaded_isr(0, &fake));
	fake = 2;
	KUNIT_EXPECT_EQ(test,
			IRQ_HANDLED,
			slsi_bt_controller_wakeup_threaded_isr(0, &fake));

	// scsc service client
	KUNIT_EXPECT_EQ(test,
			terr.level,
			bt_service_client.failure_notification(
					&bt_service_client, &terr)
			);
	KUNIT_EXPECT_FALSE(test,
			bt_service_client.stop_on_failure_v2(
					&bt_service_client, &terr)
			);
	bt_service_client.failure_reset_v2(&bt_service_client, 5, 0);
	slsi_bt_err_reset();

	KUNIT_EXPECT_EQ(test, 0, bt_service_client.suspend(&bt_service_client));
	KUNIT_EXPECT_EQ(test, 0, bt_service_client.resume(&bt_service_client));

	common_srv.wake_src = 1;
	common_srv.wake_irq = 1;

	KUNIT_SUCCEED(test);
}

static void slsi_bt_controller_getter_test(struct kunit *test)
{
	struct bhcd_boot *tp = NULL;

	kunit_info(test, "slsi_bt_controller_getter test case\n");
	// get_service
	disable_worker_for_mx = true;
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_start(SCSC_SERVICE_ID_BT));
	KUNIT_EXPECT_NOT_NULL(test, slsi_bt_controller_get_service());
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_stop(SCSC_SERVICE_ID_BT));
	KUNIT_EXPECT_EQ(test, NULL, slsi_bt_controller_get_service());

	// get qos
	KUNIT_EXPECT_EQ(test, NULL, slsi_bt_controller_get_qos());
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	KUNIT_EXPECT_EQ(test, NULL, slsi_bt_controller_get_lr_wpan_qos());
#endif

	// get boot data
	KUNIT_EXPECT_EQ(test, 0, scsc_bt_get_boot_data(&tp));
	kfree(tp);

	KUNIT_SUCCEED(test);
}

static void slsi_bt_controller_htr(struct kunit *test)
{
	struct hci_trans *prev = hci_trans_new("upper");
	struct hci_trans *htr = hci_trans_new("controller test");
	struct hci_trans *next = hci_trans_new("loopback");
	char tdata[] = {0x01, 0x02, 0x03, 0x04};
	ssize_t ret;

	kunit_info(test, "slsi_bt_controller test case\n");
	KUNIT_EXPECT_EQ(test,
			-EINVAL,
			slsi_bt_controller_transport_configure(NULL));

	// config
	disable_worker_for_mx = false;
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_transport_configure(htr));

	// send failes
	KUNIT_EXPECT_EQ(test, -EIO, htr->send(htr, tdata, sizeof(tdata), 0));

	disable_worker_for_mx = true;
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_start(SCSC_SERVICE_ID_BT));
	KUNIT_EXPECT_EQ(test,
			-EINVAL,
			htr->send(htr, tdata, sizeof(tdata), 0));
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_stop(SCSC_SERVICE_ID_BT));

	// send with loopback
	next->send_skb = loopback_tester;

	hci_trans_add_tail(htr, prev);
	hci_trans_add_tail(next, prev);

	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_start(SCSC_SERVICE_ID_BT));
	KUNIT_EXPECT_EQ(test,
			sizeof(tdata),
			htr->send(htr, tdata, sizeof(tdata), 0));
	KUNIT_EXPECT_EQ(test, 0, slsi_bt_controller_stop(SCSC_SERVICE_ID_BT));

	// deinit
	hci_trans_del(next);
	hci_trans_free(next);
	hci_trans_del(htr);
	hci_trans_free(htr);
	hci_trans_del(prev);
	hci_trans_free(prev);

	KUNIT_SUCCEED(test);
}

static struct kunit_case slsi_bt_controller_test_cases[] = {
	KUNIT_CASE(slsi_bt_controller_remove_probe_in_recovery_test),
	KUNIT_CASE(slsi_bt_controller_start_stop_test),
	KUNIT_CASE(slsi_bt_controller_pm_notifier_test),
	KUNIT_CASE(slsi_bt_controller_getter_test),
	KUNIT_CASE(slsi_bt_controller_htr),
	{}
};

static struct kunit_suite slsi_bt_controller_test_suite = {
	.name = "slsi_bt_controller_unittest",
	.test_cases = slsi_bt_controller_test_cases,
	.init = test_slsi_bt_controller_init,
	.exit = test_slsi_bt_controller_exit,
};

kunit_test_suite(slsi_bt_controller_test_suite);
