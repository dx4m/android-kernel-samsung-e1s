// SPDX-License-Identifier: GPL-2.0+
#include <kunit/test.h>

#include "kunit-common.h"
#include "../ril_notifier.c"

#define register_dev_ril_bridge_event_notifier(args...)	 kunit_mock_register_dev_ril_bridge_event_notifier(args)

static int kunit_mock_register_dev_ril_bridge_event_notifier(struct notifier_block *nb)
{
	return 0;
}

static void test_wlbt_ril_notifier(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);
	struct ril_msg {
		unsigned int dev_id;
		unsigned int data_len;
		void *data;
	} msg;
	struct cp_noti_cell_infos cp_info = {0};

	sdev->netdev[SLSI_NET_INDEX_P2PX_SWLAN] = (struct net_device *)kunit_kzalloc(test,
						 sizeof(struct net_device) + sizeof(struct netdev_vif),
						 GFP_KERNEL);
	sdev->netdev_ap = sdev->netdev[SLSI_NET_INDEX_P2PX_SWLAN];
	strscpy(sdev->netdev_ap->name, "TEST_AP", IFNAMSIZ);

	memset(&msg, 0, sizeof(struct ril_msg));
	KUNIT_EXPECT_EQ(test, -EINVAL, wlbt_ril_notifier(&sdev->ril_notifier, 0, &msg));

	wlbt_register_ril_notifier(sdev);
	KUNIT_EXPECT_EQ(test, -EINVAL, wlbt_ril_notifier(&sdev->ril_notifier, sizeof(msg), &msg));

	msg.dev_id = IPC_SYSTEM_CP_ADAPTIVE_MIPI_INFO;
	msg.data_len = 0;
	msg.data = &cp_info;
	KUNIT_EXPECT_EQ(test, -EINVAL, wlbt_ril_notifier(&sdev->ril_notifier, sizeof(msg), &msg));

	msg.data_len = sizeof(struct cp_noti_cell_infos);
	KUNIT_EXPECT_EQ(test, -22, wlbt_ril_notifier(&sdev->ril_notifier, sizeof(msg), &msg));

	sdev->device_state = SLSI_DEVICE_STATE_STARTED;
	KUNIT_EXPECT_EQ(test, 0, wlbt_ril_notifier(&sdev->ril_notifier, sizeof(msg), &msg));

	cp_info.num_cell = 2;
	cp_info.cell_list[0].rat = 3;
	cp_info.cell_list[0].band = 91;
	cp_info.cell_list[0].channel = 300;
	cp_info.cell_list[0].connection_status = 1;
	cp_info.cell_list[0].bandwidth = 10000;
	cp_info.cell_list[0].sinr = 10;
	cp_info.cell_list[0].rsrp = 0xffffff94;
	cp_info.cell_list[0].rsrq = 0xfffffff5;
	cp_info.cell_list[0].cqi = 0x0f;
	cp_info.cell_list[0].dl_mcs = 0xff;
	cp_info.cell_list[0].pusch_power = 0;
	cp_info.cell_list[1].rat = 7;
	cp_info.cell_list[1].band = 296;
	cp_info.cell_list[1].channel = 518598;
	cp_info.cell_list[1].connection_status = 2;
	cp_info.cell_list[1].bandwidth = 20000;
	cp_info.cell_list[1].sinr = 1;
	cp_info.cell_list[1].rsrp = 0xffffff8b;
	cp_info.cell_list[1].rsrq = 0xfffffff3;
	cp_info.cell_list[1].cqi = 0x00;
	cp_info.cell_list[1].dl_mcs = 0x04;
	cp_info.cell_list[1].pusch_power = 17;
	KUNIT_EXPECT_EQ(test, 0, wlbt_ril_notifier(&sdev->ril_notifier, sizeof(msg), &msg));
}

/* Test fixtures */
static int ril_notifier_test_init(struct kunit *test)
{
	test_dev_init(test);

	kunit_log(KERN_INFO, test, "%s: initialized.", __func__);
	return 0;
}

static void ril_notifier_test_exit(struct kunit *test)
{
	kunit_log(KERN_INFO, test, "%s: completed.", __func__);
}

static struct kunit_case ril_notifier_test_cases[] = {
	KUNIT_CASE(test_wlbt_ril_notifier),
	{}
};

static struct kunit_suite ril_notifier_test_suite[] = {
	{
		.name = "kunit-ril_notifier-test",
		.test_cases = ril_notifier_test_cases,
		.init = ril_notifier_test_init,
		.exit = ril_notifier_test_exit,
	}};

kunit_test_suites(ril_notifier_test_suite);
