// SPDX-License-Identifier: GPL-2.0+
#include <kunit/test.h>

#include "kunit-common.h"
#include "../lls.c"

static void test_slsi_lls_start_stats(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);

	sdev->netdev[SLSI_NET_INDEX_WLAN] = dev;
	slsi_lls_start_stats(dev, 0, 0);
}

static void test_slsi_lls_stop_stats(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);

	sdev->netdev[SLSI_NET_INDEX_WLAN] = dev;
	slsi_lls_stop_stats(sdev, 0);
}

static void test_slsi_lls_ie_to_cap(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);
	u8 *ies;

	KUNIT_EXPECT_EQ(test, 0, slsi_lls_ie_to_cap(ies, 0));
}

static void test_slsi_lls_iface_sta_stats(struct kunit *test)
{
	struct slsi_dev            *sdev = TEST_TO_SDEV(test);
	struct net_device          *dev = TEST_TO_DEV(test);
	struct netdev_vif          *ndev_vif = netdev_priv(dev);
	struct slsi_lls_iface_stat *iface_stat;

	iface_stat = kunit_kzalloc(test, sizeof(struct slsi_lls_iface_stat), GFP_KERNEL);
	slsi_lls_iface_sta_stats(sdev, ndev_vif, iface_stat);
}

static void test_slsi_lls_iface_ap_stats(struct kunit *test)
{
	struct slsi_dev            *sdev = TEST_TO_SDEV(test);
	struct net_device          *dev = TEST_TO_DEV(test);
	struct netdev_vif          *ndev_vif = netdev_priv(dev);
	struct slsi_lls_iface_stat *iface_stat;

	iface_stat = kunit_kzalloc(test, sizeof(struct slsi_lls_iface_stat), GFP_KERNEL);
	slsi_lls_iface_ap_stats(sdev, ndev_vif, iface_stat);
}

#ifdef CONFIG_SCSC_WLAN_EHT
static void test_slsi_lls_iface_ml_sta_stats(struct kunit *test)
{
	struct slsi_dev               *sdev = TEST_TO_SDEV(test);
	struct net_device             *dev = TEST_TO_DEV(test);
	struct netdev_vif             *ndev_vif = netdev_priv(dev);
	struct slsi_lls_iface_ml_stat *iface_ml_stat;

	iface_ml_stat = kunit_kzalloc(test, sizeof(struct slsi_lls_iface_ml_stat), GFP_KERNEL);
	slsi_lls_iface_ml_sta_stats(sdev, ndev_vif, iface_ml_stat);
}

static void test_slsi_lls_iface_ml_ap_stats(struct kunit *test)
{
	struct slsi_dev               *sdev = TEST_TO_SDEV(test);
	struct net_device             *dev = TEST_TO_DEV(test);
	struct netdev_vif             *ndev_vif = netdev_priv(dev);
	struct slsi_lls_iface_ml_stat *iface_ml_stat;

	iface_ml_stat = kunit_kzalloc(test, sizeof(struct slsi_lls_iface_ml_stat), GFP_KERNEL);
	slsi_lls_iface_ml_ap_stats(sdev, ndev_vif, iface_ml_stat)
}

static void test_slsi_lls_iface_ml_sta_stats_by_link(struct kunit *test)
{
	struct slsi_dev                           *sdev = TEST_TO_SDEV(test);
	struct net_device                         *dev = TEST_TO_DEV(test);
	struct netdev_vif                         *ndev_vif = netdev_priv(dev);
	struct slsi_lls_interface_link_layer_info *lls_info;
	struct slsi_lls_link_stat                 *link_stat;

	lls_info = kunit_kzalloc(test, sizeof(struct slsi_lls_interface_link_layer_info), GFP_KERNEL);
	link_stat = kunit_kzalloc(test, sizeof(slsi_lls_link_stat), GFP_KERNEL);
	slsi_lls_iface_ml_sta_stats_by_link(sdev, ndev_vif, lls_info, link_stat);
}

static void test_slsi_lls_iface_ml_ap_stats_by_link(struct kunit *test)
{
	struct slsi_dev                           *sdev = TEST_TO_SDEV(test);
	struct net_device                         *dev = TEST_TO_DEV(test);
	struct netdev_vif                         *ndev_vif = netdev_priv(dev);
	struct slsi_lls_interface_link_layer_info *lls_info;
	struct slsi_lls_link_stat                 *link_stat;

	lls_info = kunit_kzalloc(test, sizeof(struct slsi_lls_interface_link_layer_info), GFP_KERNEL);
	link_stat = kunit_kzalloc(test, sizeof(slsi_lls_link_stat), GFP_KERNEL);
	slsi_lls_iface_ml_ap_stats_by_link(sdev, ndev_vif, link_stat);
}

static void test_slsi_lls_iface_ml_stat_fill_by_link(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);
	struct net_device         *dev = TEST_TO_DEV(test);
	struct slsi_lls_link_stat *link_stat;

	link_stat = kunit_kzalloc(test, sizeof(slsi_lls_link_stat), GFP_KERNEL);
	slsi_lls_iface_ml_stat_fill_by_link(sdev, dev,link_stat);
}

static void test_slsi_lls_iface_ml_stat_fill(struct kunit *test)
{
	struct slsi_dev               *sdev = TEST_TO_SDEV(test);
	struct net_device             *dev = TEST_TO_DEV(test);
	struct slsi_lls_iface_ml_stat *iface_ml_stat;

	iface_ml_stat = kunit_kzalloc(test, sizeof(struct slsi_lls_iface_ml_stat), GFP_KERNEL);
	slsi_lls_iface_ml_stat_fill_by_link(sdev, dev, iface_ml_stat);
}
#endif

static void test_slsi_lls_iface_stat_fill(struct kunit *test)
{
	struct slsi_dev            *sdev = TEST_TO_SDEV(test);
	struct net_device          *dev = TEST_TO_DEV(test);
	struct slsi_lls_iface_stat *iface_stat;

	iface_stat = kunit_kzalloc(test, sizeof(struct slsi_lls_iface_stat), GFP_KERNEL);
	sdev->device_config.domain_info.regdomain = kunit_kzalloc(test, sizeof(struct ieee80211_regdomain), GFP_KERNEL);
	slsi_lls_iface_stat_fill(sdev, dev, iface_stat);
}

static void test_slsi_lls_fill(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);
	u8              *buf = NULL;

	KUNIT_EXPECT_EQ(test, -EIO, slsi_lls_fill(sdev, &buf));
}

#ifdef CONFIG_SCSC_WLAN_EHT
static void test_slsi_lls_fill_mlo(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);
	u8              *buf = NULL;

	KUNIT_EXPECT_EQ(test, -EIO, slsi_lls_fill_mlo(sdev, &buf));
}
#endif

static void test_slsi_lls_fill_stats(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);
	u8              *buf = NULL;

	KUNIT_EXPECT_EQ(test, -EIO, slsi_lls_fill_stats(sdev, &buf, false));
}

/*
 * Test fictures
 */
static int lls_test_init(struct kunit *test)
{
	test_dev_init(test);

	kunit_log(KERN_INFO, test, "%s: initialized.", __func__);
	return 0;
}

static void lls_test_exit(struct kunit *test)
{
	kunit_log(KERN_INFO, test, "%s: completed.", __func__);
	return;
}

/*
 * KUnit testcase definitions
 */
static struct kunit_case lls_test_cases[] = {
	KUNIT_CASE(test_slsi_lls_start_stats),
	KUNIT_CASE(test_slsi_lls_stop_stats),
	KUNIT_CASE(test_slsi_lls_ie_to_cap),
	KUNIT_CASE(test_slsi_lls_iface_sta_stats),
	KUNIT_CASE(test_slsi_lls_iface_ap_stats),
#ifdef CONFIG_SCSC_WLAN_EHT
	KUNIT_CASE(test_slsi_lls_iface_ml_sta_stats),
	KUNIT_CASE(test_slsi_lls_iface_ml_ap_stats),
	KUNIT_CASE(test_slsi_lls_iface_ml_sta_stats_by_link),
	KUNIT_CASE(test_slsi_lls_iface_ml_ap_stats_by_link),
	KUNIT_CASE(test_slsi_lls_iface_ml_stat_fill_by_link),
	KUNIT_CASE(test_slsi_lls_iface_ml_stat_fill),
#endif
	KUNIT_CASE(test_slsi_lls_iface_stat_fill),
	KUNIT_CASE(test_slsi_lls_fill),
#ifdef CONFIG_SCSC_WLAN_EHT
	KUNIT_CASE(test_slsi_lls_fill_mlo),
#endif
	KUNIT_CASE(test_slsi_lls_fill_stats),
	{}
};

static struct kunit_suite lls_test_suite[] = {
	{
		.name = "kunit-lls-test",
		.test_cases = lls_test_cases,
		.init = lls_test_init,
		.exit = lls_test_exit,
	}};

kunit_test_suites(lls_test_suite);
