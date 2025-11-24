// SPDX-License-Identifier: GPL-2.0+
#include <kunit/test.h>
#include "kunit-mock-kernel.h"
#include "kunit-mock-mlme.h"
#include "kunit-mock-misc.h"
#include "kunit-mock-load_manager.h"
#include "kunit-mock-txbp.h"
#include "kunit-mock-cm_if.h"
#include "kunit-mock-reg_info.h"
#include "kunit-mock-dev.h"
#include "kunit-mock-nl80211_vendor.h"
#include "kunit-mock-nl80211_vendor_nan.h"
#include "kunit-mock-netif.h"
#include "kunit-mock-ba.h"
#include "kunit-mock-hip4.h"
#include "kunit-mock-tdls_manager.h"
#include "kunit-mock-cac.h"
#include "kunit-mock-cfg80211_ops.h"
#include "kunit-mock-qsfs.h"
#include "kunit-mock-ini_config.h"
#include "kunit-mock-mib.h"
#include "kunit-mock-rx.h"
#include "kunit-mock-hip.h"
#include "kunit-mock-log2us.h"
#include "../mgt.c"

#define SLSI_DEFAULT_BSSID		"\x01\x02\xDF\x1E\xB2\x39"

static void test_mgt_slsi_EnableRfTestMode_set(struct kunit *test)
{
	char val[1] = {'1'};
	struct kernel_param *kp = kunit_kzalloc(test, sizeof(struct kernel_param), GFP_KERNEL);
	int enableRfTestMode = 0;

	kp->arg = &enableRfTestMode;
	KUNIT_EXPECT_EQ(test, 0, slsi_EnableRfTestMode_set(val, kp));
	KUNIT_EXPECT_EQ(test, 1, enableRfTestMode);

	val[0] = '5';
	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_EnableRfTestMode_set(val, kp));
	KUNIT_EXPECT_EQ(test, 1, enableRfTestMode);
}

static void test_mgt_slsi_EnableRfTestMode_get(struct kunit *test)
{
	char buf[100];

	KUNIT_EXPECT_EQ(test, 2, slsi_EnableRfTestMode_get(buf, NULL));
}

static void test_mgt_slsi_monitor_vif_num_set_cb(struct kunit *test)
{
	struct slsi_dev      *sdev = TEST_TO_SDEV(test);
	struct net_device    *dev = TEST_TO_DEV(test);
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	char val[1] = {'1'};

	test_cm_if_set_sdev(sdev);
	sdev->netdev[SLSI_NET_INDEX_WLAN] = NULL;
	KUNIT_EXPECT_EQ(test, 0, slsi_monitor_vif_num_set_cb(val, NULL));

	ndev_vif->is_available = false;
	sdev->netdev[SLSI_NET_INDEX_WLAN] = dev;
	KUNIT_EXPECT_EQ(test, 0, slsi_monitor_vif_num_set_cb(val, NULL));

	ndev_vif->is_available = true;
	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	KUNIT_EXPECT_EQ(test, 0, slsi_monitor_vif_num_set_cb(val, NULL));

	ndev_vif->vif_type = FAPI_VIFTYPE_MONITOR;
	KUNIT_EXPECT_EQ(test, 0, slsi_monitor_vif_num_set_cb(val, NULL));

	monitor_vif_num = 1;
	val[0] = '2';
	KUNIT_EXPECT_EQ(test, 0, slsi_monitor_vif_num_set_cb(val, NULL));
}

static void test_mgt_slsi_monitor_vif_num_get_cb(struct kunit *test)
{
	char buf[100];

	KUNIT_EXPECT_EQ(test, 2, slsi_monitor_vif_num_get_cb(buf, NULL));
}

static void test_mgt_sysfs_show_macaddr(struct kunit *test)
{
	char buf[100];

	KUNIT_EXPECT_GE(test, sizeof(sysfs_mac_override), sysfs_show_macaddr(NULL, NULL, buf));
}

static void test_mgt_sysfs_store_macaddr(struct kunit *test)
{
	char buf[] = "00:ff:ff:ff:ff:ff";
	ssize_t count = 10;

	KUNIT_EXPECT_EQ(test, count, sysfs_store_macaddr(NULL, NULL, buf, (size_t)count));
}

static void test_mgt_slsi_create_destroy_sysfs_macaddr(struct kunit *test)
{
	slsi_create_sysfs_macaddr();
	slsi_destroy_sysfs_macaddr();
}

static void test_mgt_sysfs_show_version_info(struct kunit *test)
{
	char buf[512];

	KUNIT_EXPECT_GE(test, sizeof(buf), sysfs_show_version_info(NULL, NULL, buf));
}

static void test_mgt_slsi_create_destroy_sysfs_version_info(struct kunit *test)
{
	slsi_create_sysfs_version_info();
	slsi_destroy_sysfs_version_info();
}

static void test_mgt_sysfs_show_debugdump(struct kunit *test)
{
	char buf[100];

	KUNIT_EXPECT_LT(test, 0, sysfs_show_debugdump(NULL, NULL, buf));
}

static void test_mgt_sysfs_store_debugdump(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);
	char            buf[100];
	ssize_t         count = 10;

	test_cm_if_set_sdev(sdev);
	KUNIT_EXPECT_EQ(test, count, sysfs_store_debugdump(NULL, NULL, buf, (size_t)count));
}

static void test_mgt_slsi_create_destroy_sysfs_debug_dump(struct kunit *test)
{
	slsi_create_sysfs_debug_dump();
	slsi_destroy_sysfs_debug_dump();
}

static void test_mgt_slsi_purge_scan_results_locked(struct kunit *test)
{
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	struct slsi_scan_result *scan_result = kmalloc(sizeof(*scan_result), GFP_KERNEL);

	scan_result->beacon = NULL;
	scan_result->probe_resp = NULL;
	scan_result->next = NULL;
	ndev_vif->scan[0].scan_results = scan_result;

	slsi_purge_scan_results_locked(ndev_vif, 0);
}

static void test_mgt_slsi_purge_scan_results(struct kunit *test)
{
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	struct slsi_scan_result *scan_result = kmalloc(sizeof(*scan_result), GFP_KERNEL);

	scan_result->beacon = NULL;
	scan_result->probe_resp = NULL;
	scan_result->next = NULL;
	ndev_vif->scan[0].scan_results = scan_result;

	slsi_purge_scan_results(ndev_vif, 0);
}

static void test_mgt_slsi_purge_blacklist(struct kunit *test)
{
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);

	ndev_vif->acl_data_supplicant = NULL;
	ndev_vif->acl_data_hal = NULL;

	INIT_LIST_HEAD(&ndev_vif->acl_data_fw_list);
	INIT_LIST_HEAD(&ndev_vif->acl_data_ioctl_list);

	slsi_purge_blacklist(ndev_vif);
}

static void test_mgt_slsi_dequeue_cached_scan_result(struct kunit *test)
{
	struct slsi_scan *scan = kunit_kzalloc(test, sizeof(*scan), GFP_KERNEL);
	struct sk_buff   *skb = kunit_kzalloc(test, sizeof(*skb), GFP_KERNEL);
	int              count = 0;

	scan->scan_results = kmalloc(sizeof(struct slsi_scan_result), GFP_KERNEL);
	scan->scan_results->beacon = skb;
	KUNIT_EXPECT_PTR_EQ(test, skb, slsi_dequeue_cached_scan_result(scan, &count));

	scan->scan_results = kmalloc(sizeof(struct slsi_scan_result), GFP_KERNEL);
	scan->scan_results->beacon = NULL;
	scan->scan_results->probe_resp = skb;
	KUNIT_EXPECT_PTR_EQ(test, skb, slsi_dequeue_cached_scan_result(scan, &count));

	scan->scan_results = kmalloc(sizeof(struct slsi_scan_result), GFP_KERNEL);
	scan->scan_results->probe_resp = NULL;
	KUNIT_EXPECT_PTR_EQ(test, NULL, slsi_dequeue_cached_scan_result(scan, &count));
}

static void test_mgt_slsi_copy_mac_valid(struct kunit *test)
{
	u8   dest[ETH_ALEN];
	u32  src[ETH_ALEN];
	char mac_addr[] = "00:ff:ff:ff:ff:ff";

	sscanf(mac_addr_override, "%02X:%02X:%02X:%02X:%02X:%02X",
	       &src[0], &src[1], &src[2], &src[3], &src[4], &src[5]);
	slsi_copy_mac_valid(dest, src);
}

static void test_mgt_slsi_get_hw_mac_address(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);
	u8              addr[ETH_ALEN];

	slsi_get_hw_mac_address(sdev, addr);
}

static void test_mgt_write_wifi_version_info_file(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);

	write_wifi_version_info_file(sdev);
}

static void test_mgt_write_m_test_chip_version_file(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);

	write_m_test_chip_version_file(sdev);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static void test_mgt_write_softap_info_file(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);

	write_softap_info_file(sdev);
}
#endif

static void test_mgt_slsi_start_monitor_mode(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);

	slsi_start_monitor_mode(sdev, dev);
}

static void test_mgt_slsi_stop_monitor_mode(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);

	slsi_stop_monitor_mode(sdev, dev);
}

#if IS_ENABLED(CONFIG_SCSC_LOG_COLLECTION)
static void test_mgt_slsi_hcf_collect(struct kunit *test)
{
	struct slsi_dev                  *sdev = TEST_TO_SDEV(test);
	struct scsc_log_collector_client *collect_client;
	size_t                           size = 0;

	collect_client = kunit_kzalloc(test, sizeof(*collect_client), GFP_KERNEL);
	collect_client->prv = sdev;

	sdev->collect_mib.num_files = 1;
	sdev->collect_mib.enabled = false;
	KUNIT_EXPECT_EQ(test, 0, slsi_hcf_collect(collect_client, size));
}
#endif

static void test_mgt_slsi_clear_sys_error_buffer(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);

	sdev->sys_error_log_buf.pos = 10;
	sdev->sys_error_log_buf.log_buf = kmalloc(sdev->sys_error_log_buf.pos + 1, GFP_KERNEL);
	slsi_clear_sys_error_buffer(sdev);
	kfree(sdev->sys_error_log_buf.log_buf);
}

static void test_mgt_slsi_wlan_recovery_init(struct kunit *test)
{
	struct slsi_dev *sdev = TEST_TO_SDEV(test);

	slsi_wlan_recovery_init(sdev);
}

static void test_mgt_slsi_start(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct firmwrae   *firmware;
	u8                index;

	sdev->maxwell_core = 1;
	sdev->device_config.domain_info.regdomain = kunit_kzalloc(test,
								  sizeof(struct ieee80211_regdomain),
								  GFP_KERNEL);
	kunit_mock_mx140_request_file(sdev->maxwell_core, NULL, &firmware);

	sdev->device_state = SLSI_DEVICE_STATE_ATTACHING;
	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_start(sdev, dev));

	sdev->device_state = SLSI_DEVICE_STATE_STARTING;
	KUNIT_EXPECT_EQ(test, 0, slsi_start(sdev, dev));

	sdev->device_state = SLSI_DEVICE_STATE_STOPPED;
	sdev->mac_changed = false;
	sdev->default_scan_ies = NULL;
	sdev->recovery_status = 1;
	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_start(sdev, dev));

	for (index = 0; index <= sdev->collect_mib.num_files; index++)
		kfree(sdev->collect_mib.file[index].data);

	kunit_mock_mx140_release_file(sdev->maxwell_core, firmware);
}

static void test_mgt_slsi_dynamic_interface_create(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct vif_params *params = kunit_kzalloc(test, sizeof(*params), GFP_KERNEL);

	sdev->netdev[0] = dev;
	KUNIT_EXPECT_PTR_EQ(test, dev, slsi_dynamic_interface_create(sdev->wiphy, NULL,
								     NL80211_IFTYPE_STATION,
								     params, false));
}

static void test_mgt_slsi_stop_chip(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);

	slsi_stop_chip(sdev);
}

#ifdef CONFIG_SCSC_WIFI_NAN_ENABLE
static void test_mgt_slsi_ndl_vif_cleanup(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);

	slsi_ndl_vif_cleanup(sdev, dev, true);
}
#endif

static void test_mgt_slsi_vif_cleanup(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);

	slsi_vif_cleanup(sdev, dev, true, false);
}

static void test_mgt_slsi_sched_scan_stopped(struct kunit *test)
{
	struct net_device  *dev = TEST_TO_DEV(test);
	struct netdev_vif  *ndev_vif = netdev_priv(dev);
	struct work_struct *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);

	ndev_vif->sched_scan_stop_wk = *work;
	ndev_vif->sdev = TEST_TO_SDEV(test);
	ndev_vif->scan[SLSI_SCAN_SCHED_ID].sched_req = kunit_kzalloc(test,
								     sizeof(struct cfg80211_sched_scan_request),
								     GFP_KERNEL);
	slsi_sched_scan_stopped(&ndev_vif->sched_scan_stop_wk);
}

static void test_mgt_slsi_scan_cleanup(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);

	slsi_scan_cleanup(sdev, dev);
}

static void test_mgt_slsi_clear_low_latency_state(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_INIT(sdev->device_config_mutex);
	ndev_vif->ifnum = SLSI_NET_INDEX_WLAN;
	slsi_clear_low_latency_state(dev);
}

static void test_mgt_slsi_stop_net_dev_locked(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	ndev_vif->is_available = true;
	sdev->netdev_up_count = 1;
	slsi_stop_net_dev_locked(sdev, dev, true);

	ndev_vif->is_available = false;
	ndev_vif->ifnum = SLSI_NAN_DATA_IFINDEX_START;
	ndev_vif->activated = true;
	slsi_stop_net_dev_locked(sdev, dev, true);

	ndev_vif->is_available = true;
	ndev_vif->traffic_mon_state = TRAFFIC_MON_CLIENT_STATE_MID;
	sdev->netdev[1] = dev;
	slsi_stop_net_dev_locked(sdev, dev, true);

	ndev_vif->is_available = true;
	ndev_vif->traffic_mon_state = TRAFFIC_MON_CLIENT_STATE_MID;
	sdev->netdev[1] = dev;
	slsi_stop_net_dev_locked(sdev, dev, true);
}

static void test_mgt_slsi_stop_net_dev(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	ndev_vif->is_available = false;
	ndev_vif->ifnum = SLSI_NAN_DATA_IFINDEX_START - 1;
	slsi_stop_net_dev(sdev, dev);
}

static void test_mgt_slsi_stop(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);

	slsi_stop(sdev);
}

#ifdef CONFIG_WLBT_LOCAL_MIB
static void test_mgt_slsi_mib_slice(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	u16 psid = SLSI_PSID_UNIFI_EHT_ACTIVATED;
	u16 pslen = 5;
	u32 length = 10;
	u8 data[100];
	u32 p_parsed_len;
	u32 p_mib_slice_len;
	u8 *mib_slice = NULL;

	SLSI_U16_TO_BUFF_LE(psid, data);
	SLSI_U16_TO_BUFF_LE(pslen - 4, &data[2]);

	mib_slice = slsi_mib_slice(sdev, data, length, &p_parsed_len, &p_mib_slice_len);
	KUNIT_EXPECT_NOT_NULL(test, mib_slice);
	kfree(mib_slice);
}
#endif
static void test_mgt_slsi_mib_get_platform(struct kunit *test)
{
	struct slsi_dev_mib_info *mib_info = kunit_kzalloc(test, sizeof(*mib_info), GFP_KERNEL);
	u8                       mib_data[12];

	mib_info->mib_len = sizeof(mib_data) - 1;
	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_mib_get_platform(mib_info));

	mib_info->mib_len = sizeof(mib_data);
	mib_info->mib_data = kunit_kzalloc(test, sizeof(*mib_data), GFP_KERNEL);
	mib_info->mib_data[0] = 0xFE;
	mib_info->mib_data[1] = 0xFF;
	mib_info->mib_data[2] = 0x01;
	mib_info->mib_data[3] = 0x00;
	mib_info->mib_data[4] = 0x01;
	mib_info->mib_data[5] = 0x00;
	mib_info->mib_data[6] = 0xFF;
	mib_info->mib_data[7] = 0xFF;
	mib_info->mib_data[8] = 0x02;
	mib_info->mib_data[9] = 0x00;
	mib_info->mib_data[10] = 0x21;
	KUNIT_EXPECT_EQ(test, 0, slsi_mib_get_platform(mib_info));
}

static void test_mgt_slsi_mib_open_file(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct slsi_dev_mib_info *mib_info = kunit_kzalloc(test, sizeof(*mib_info), GFP_KERNEL);
	struct firmware          *fw = kunit_kzalloc(test, sizeof(*fw), GFP_KERNEL);

	mib_info->mib_file_name = "test_mib_file";
	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_mib_open_file(sdev, mib_info, &fw));

	mib_info->mib_file_name = "test_mib_file.hcf";
	KUNIT_EXPECT_EQ(test, -ENOENT, slsi_mib_open_file(sdev, mib_info, &fw));

	sdev->collect_mib.num_files = SLSI_WLAN_MAX_MIB_FILE + 1;
	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_mib_open_file(sdev, mib_info, &fw));
}

static void test_mgt_slsi_mib_close_file(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct firmware          *fw = kunit_kzalloc(test, sizeof(*fw), GFP_KERNEL);

	KUNIT_EXPECT_EQ(test, 0, slsi_mib_close_file(sdev, fw));
}

#ifdef CONFIG_WLBT_LOCAL_MIB
static void test_mgt_slsi_mib_download_file(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct slsi_dev_mib_info *mib_info = kunit_kzalloc(test, sizeof(*mib_info), GFP_KERNEL);
	struct firmware          *fw = kunit_kzalloc(test, sizeof(*fw), GFP_KERNEL);

	mib_info->mib_file_name = "test_mib_file";
	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_mib_download_file(sdev, mib_info));
}
#endif

static void test_mgt_slsi_process_supported_channels(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);
	struct slsi_mib_get_entry get_values[] = {{SLSI_PSID_UNIFI_TWT_CONTROL_FLAGS, { 0, 0 } } };
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_value     *values;
	int                       mib_index = 0;

	mibrsp.dataLength = 10;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	values = slsi_read_mibs(sdev, NULL, get_values, 1, &mibrsp);

	KUNIT_EXPECT_EQ(test, 0, slsi_process_supported_channels(sdev, values, mib_index));

	kfree(mibrsp.data);
	kfree(values);
}

static void test_mgt_slsi_get_ht_vht_capabilities(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);
	struct slsi_mib_get_entry get_values[] = {{SLSI_PSID_UNIFI_TWT_CONTROL_FLAGS, { 0, 0 } } };
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_value     *values;
	int                       mib_index = 0;

	mibrsp.dataLength = 10;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	values = slsi_read_mibs(sdev, NULL, get_values, 1, &mibrsp);

	slsi_get_ht_vht_capabilities(sdev, values, &mib_index);

	kfree(mibrsp.data);
	kfree(values);
}

#ifdef CONFIG_SCSC_WLAN_SUPPORT_6G
static void test_mgt_slsi_get_6g_capabilities(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);
	struct slsi_mib_get_entry get_values[] = {{SLSI_PSID_UNIFI_TWT_CONTROL_FLAGS, { 0, 0 } } };
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_value     *values;
	int                       mib_index = 0;

	mibrsp.dataLength = 10;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	values = slsi_read_mibs(sdev, NULL, get_values, 1, &mibrsp);

	slsi_get_6g_capabilities(sdev, values, &mib_index);

	kfree(mibrsp.data);
	kfree(values);
}
#endif

#if defined(CONFIG_SCSC_WLAN_TAS)
static void test_mgt_slsi_tas_get_config(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);
	struct slsi_mib_get_entry get_values[] = {{SLSI_PSID_UNIFI_TWT_CONTROL_FLAGS, { 0, 0 } } };
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_value     *values;
	int                       mib_index = 0;

	mibrsp.dataLength = 10;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	values = slsi_read_mibs(sdev, NULL, get_values, 1, &mibrsp);

	slsi_tas_get_config(sdev, values, &mib_index);

	kfree(mibrsp.data);
	kfree(values);
}
#endif

#ifdef CONFIG_SCSC_WLAN_EHT
static void test_mgt_slsi_get_mlo_capabilities(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);
	struct slsi_mib_get_entry get_values[] = {{SLSI_PSID_UNIFI_TWT_CONTROL_FLAGS, { 0, 0 } } };
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_value     *values;
	int                       mib_index = 0;

	mibrsp.dataLength = 10;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	values = slsi_read_mibs(sdev, NULL, get_values, 1, &mibrsp);

	slsi_get_mlo_capabilities(sdev, values, &mib_index);

	kfree(mibrsp.data);
	kfree(values);
}
#endif

static void test_mgt_slsi_get_mib_entry_value(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);
	struct slsi_mib_get_entry get_values[] = {{SLSI_PSID_UNIFI_TWT_CONTROL_FLAGS, { 0, 0 } } };
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_value     *values;

	mibrsp.dataLength = 10;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	sdev->recovery_timeout = 99;
	values = slsi_read_mibs(sdev, NULL, get_values, 16, &mibrsp);

	KUNIT_EXPECT_EQ(test, 0, slsi_get_mib_entry_value(sdev, values));

	kfree(mibrsp.data);
	kfree(values);
}

static void test_mgt_slsi_mib_initial_get(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_mib_initial_get(sdev));
}

static void test_mgt_slsi_set_mib_roam(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_set_mib_roam(sdev, dev, 1, 0));
}

static void test_mgt_slsi_twt_update_ctrl_flags(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_twt_update_ctrl_flags(dev, 1));
}

#ifdef CONFIG_SCSC_WLAN_SET_PREFERRED_ANTENNA
static void test_mgt_slsi_set_mib_preferred_antenna(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_set_mib_preferred_antenna(dev, 1));
}
#endif

static void test_mgt_slsi_reset_throughput_stats(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);

	slsi_reset_throughput_stats(dev);
}

static void test_mgt_slsi_get_mib_roam(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	int               mib_value;

	KUNIT_EXPECT_EQ(test, 0, slsi_get_mib_roam(sdev, 1, &mib_value));
}

#ifdef CONFIG_SCSC_WLAN_GSCAN_ENABLE
static void test_mgt_slsi_mib_get_gscan_cap(struct kunit *test)
{
	struct slsi_dev                   *sdev = TEST_TO_SDEV(test);
	struct slsi_nl_gscan_capabilities *cap = kunit_kzalloc(test, sizeof(*cap), GFP_KERNEL);

	KUNIT_EXPECT_EQ(test, 0, slsi_mib_get_gscan_cap(sdev, cap));
}
#endif

static void test_mgt_slsi_mib_get_apf_cap(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_mib_get_apf_cap(sdev, dev));
}

static void test_mgt_slsi_mib_get_rtt_cap(struct kunit *test)
{
	struct slsi_dev                   *sdev = TEST_TO_SDEV(test);
	struct net_device                 *dev = TEST_TO_DEV(test);
	struct slsi_nl_gscan_capabilities *cap = kunit_kzalloc(test, sizeof(*cap), GFP_KERNEL);

	sdev->recovery_timeout = 99;
	KUNIT_EXPECT_EQ(test, 0, slsi_mib_get_rtt_cap(sdev, dev, cap));
}

static void test_mgt_slsi_mib_get_sta_tdls_activated(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	bool              tdls_supported;

	sdev->recovery_timeout = 2;
	KUNIT_EXPECT_EQ(test, 0, slsi_mib_get_sta_tdls_activated(sdev, dev, &tdls_supported));
}

static void test_mgt_slsi_mib_get_sta_tdls_max_peer(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	sdev->recovery_timeout = 2;
	KUNIT_EXPECT_EQ(test, 0, slsi_mib_get_sta_tdls_max_peer(sdev, dev, ndev_vif));
}

#ifdef CONFIG_SCSC_WLAN_EHT
static void test_mgt_slsi_get_link_peer_from_mac(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	KUNIT_EXPECT_NOT_NULL(test, slsi_get_link_peer_from_mac(dev, SLSI_DEFAULT_HW_MAC_ADDR));
}

static void test_mgt_slsi_sta_add_peer_link(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	ndev_vif->activated = true;
	KUNIT_EXPECT_NOT_NULL(test, slsi_sta_add_peer_link(sdev, dev, SLSI_DEFAULT_HW_MAC_ADDR, 1));
}
#endif

static void test_mgt_slsi_peer_add(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_peer  *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);
	struct slsi_peer  *peer2 = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);
	u8                *mac = SLSI_DEFAULT_HW_MAC_ADDR;
	u16               aid = 1;

	ndev_vif->activated = true;
	ndev_vif->vif_type = FAPI_VIFTYPE_AP;
	ndev_vif->sta.tdls_enabled = true;
	ndev_vif->peer_sta_record[0] = peer;
	peer->valid = true;
	peer->queueset = 0;
	SLSI_ETHER_COPY(peer->address, mac);
	KUNIT_EXPECT_NULL(test, slsi_peer_add(sdev, dev, mac, aid));

	aid = 2;
	ndev_vif->peer_sta_record[1] = peer2;
	KUNIT_EXPECT_NOT_NULL(test, slsi_peer_add(sdev, dev, mac, aid));
}

static void test_mgt_slsi_peer_reset_stats(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct slsi_peer  *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);

	slsi_peer_reset_stats(sdev, dev, peer);
}

static void test_mgt_slsi_dump_stats(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);

	slsi_dump_stats(dev);
}

#if defined(CONFIG_SCSC_WLAN_ENHANCED_BIGDATA) && (defined(SCSC_SEP_VERSION) && SCSC_SEP_VERSION >= 10)
static void test_mgt_slsi_substitute_null(struct kunit *test)
{
	char s[1] = {' ',};

	slsi_substitute_null(s, 'a', 1);
}

static void test_mgt_slsi_fill_bigdata_record(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);
	struct scsc_hanged_record *hr = kunit_kzalloc(test, sizeof(*hr), GFP_KERNEL);
	char result[100];

	slsi_fill_bigdata_record(sdev, hr, result, 1234, 100);
}
#endif

static void test_mgt_slsi_send_hanged_vendor_event(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);

	sdev->wiphy->n_vendor_events = SLSI_NL80211_VENDOR_HANGED_EVENT + 1;
	KUNIT_EXPECT_EQ(test, 0, slsi_send_hanged_vendor_event(sdev, 1234));
}

static void test_mgt_slsi_send_power_measurement_vendor_event(struct kunit *test)
{
	struct slsi_dev           *sdev = TEST_TO_SDEV(test);

	sdev->wiphy->n_vendor_events = SLSI_NL80211_VENDOR_POWER_MEASUREMENT_EVENT + 1;
	KUNIT_EXPECT_EQ(test, 0, slsi_send_power_measurement_vendor_event(sdev, 1));
}

static void test_mgt_slsi_set_ext_cap(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	u8                ies[10];
	int               ie_len = 10;
	u8                ext_cap_mask[10];

	ies[1] = 10;
	KUNIT_EXPECT_EQ(test, 0, slsi_set_ext_cap(sdev, dev, ies, ie_len, ext_cap_mask));
}

static void test_mgt_slsi_search_ies_for_qos_indicators(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);

	KUNIT_EXPECT_FALSE(test, slsi_search_ies_for_qos_indicators(sdev, NULL, 1));
}

static void test_mgt_slsi_peer_update_assoc_req(struct kunit *test)
{
	struct slsi_dev       *sdev = TEST_TO_SDEV(test);
	struct net_device     *dev = TEST_TO_DEV(test);
	struct netdev_vif     *ndev_vif = netdev_priv(dev);
	struct slsi_peer      *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);
	struct sk_buff        *skb;
	struct ieee80211_mgmt *mgmt;

	peer->assoc_ie = NULL;
	skb = fapi_alloc(mlme_procedure_started_ind, MLME_PROCEDURE_STARTED_IND, 0, 128);
	ndev_vif->vif_type = FAPI_VIFTYPE_NAN;
	slsi_peer_update_assoc_req(sdev, dev, peer, skb);

	skb = fapi_alloc(mlme_roamed_ind, MLME_ROAMED_IND, 0, 128);
	slsi_peer_update_assoc_req(sdev, dev, peer, skb);

	skb = fapi_alloc(mlme_procedure_started_ind, MLME_PROCEDURE_STARTED_IND, 0, 128);
	mgmt = fapi_get_mgmt(skb);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ASSOC_REQ);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_REASSOC_REQ);
	ndev_vif->vif_type = FAPI_VIFTYPE_AP;
	slsi_peer_update_assoc_req(sdev, dev, peer, skb);
}

static void test_mgt_slsi_peer_update_assoc_rsp(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_peer  *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);
	struct sk_buff    *skb;

	peer->assoc_resp_ie = NULL;
	skb = fapi_alloc(mlme_reassociate_ind, MLME_REASSOCIATE_IND, 0, 128);
	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	slsi_peer_update_assoc_rsp(sdev, dev, peer, skb);
}

static void test_mgt_slsi_peer_remove(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct slsi_peer  *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);
	int               i;

	for (i = 0; i < NUM_BA_SESSIONS_PER_PEER; i++)
		skb_queue_head_init(&peer->buffered_frames[i]);
	KUNIT_EXPECT_EQ(test, 0, slsi_peer_remove(sdev, dev, peer));
}

static void test_mgt_slsi_vif_activated(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	ndev_vif->activated = false;
	ndev_vif->vif_type = FAPI_VIFTYPE_AP;
	KUNIT_EXPECT_EQ(test, 0, slsi_vif_activated(sdev, dev));

	ndev_vif->activated = false;
	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	KUNIT_EXPECT_EQ(test, 0, slsi_vif_activated(sdev, dev));

	ndev_vif->activated = true;
	KUNIT_EXPECT_EQ(test, -EALREADY, slsi_vif_activated(sdev, dev));
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

static void test_mgt_slsi_vif_deactivated(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	ndev_vif->iftype = NL80211_IFTYPE_STATION;
	ndev_vif->sta.tdls_enabled = true;
	ndev_vif->sta.sta_bss = NULL;

	INIT_LIST_HEAD(&ndev_vif->sta.tdls_candidate_setup_list);
	slsi_vif_deactivated(sdev, dev);

	ndev_vif->vif_type = FAPI_VIFTYPE_AP;
	ndev_vif->iftype = NL80211_IFTYPE_P2P_CLIENT;
	slsi_vif_deactivated(sdev, dev);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

static void test_mgt_slsi_sta_ieee80211_mode(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u16               current_bss_channel_frequency = 5000;

	ndev_vif->sta.sta_bss = kunit_kzalloc(test, sizeof(struct cfg80211_bss), GFP_KERNEL);
	ndev_vif->sta.sta_bss->ies = kunit_kzalloc(test, sizeof(struct cfg80211_bss_ies), GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, SLSI_80211_MODE_11N, slsi_sta_ieee80211_mode(current_bss_channel_frequency,
									   ndev_vif->sta.sta_bss->ies->data,
									   ndev_vif->sta.sta_bss->ies->len));
}

static void test_mgt_slsi_populate_bss_record(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct cfg80211_bss_ies *ies = kunit_kzalloc(test, sizeof(struct cfg80211_bss_ies) + sizeof(u8) * 20,
						     GFP_KERNEL);

	ndev_vif->sdev->recovery_timeout = 9;
	ndev_vif->sta.sta_bss = kunit_kzalloc(test, sizeof(struct cfg80211_bss), GFP_KERNEL);
	memcpy(ndev_vif->sta.sta_bss->bssid, SLSI_DEFAULT_BSSID, 6);
	ndev_vif->sta.sta_bss->ies = ies;

#ifdef CONFIG_SCSC_WLAN_EHT
	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_populate_bss_record(dev, 0));
	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_populate_bss_record(dev, 1));
#else
	KUNIT_EXPECT_EQ(test, 0, slsi_populate_bss_record(dev));
#endif
}

static void test_mgt_slsi_fill_ap_sta_info_from_peer(struct kunit *test)
{
	struct net_device       *dev = TEST_TO_DEV(test);
	struct slsi_ap_sta_info *ap_sta_info = kunit_kzalloc(test, sizeof(*ap_sta_info), GFP_KERNEL);
	struct slsi_peer        *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);

	peer->assoc_ie = alloc_skb(100, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 0, slsi_fill_ap_sta_info_from_peer(dev, ap_sta_info, peer));

	kfree_skb(peer->assoc_ie);
}

static void test_mgt_slsi_fill_ap_sta_info_stats(struct kunit *test)
{
	struct slsi_dev      *sdev = TEST_TO_SDEV(test);
	struct net_device    *dev = TEST_TO_DEV(test);
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	u32                  rate_stats[SLSI_MAX_NUM_SUPPORTED_RATES];

	KUNIT_EXPECT_EQ(test, 0, slsi_fill_ap_sta_info_stats(sdev, dev, rate_stats, 1, 1));
}

static void test_mgt_slsi_fill_ap_sta_info(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	struct slsi_ap_sta_info *ap_sta_info = kunit_kzalloc(test, sizeof(*ap_sta_info), GFP_KERNEL);
	u8                      *peer_mac = SLSI_DEFAULT_HW_MAC_ADDR;

	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]->valid = true;
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]->assoc_ie = alloc_skb(100, GFP_KERNEL);
	ap_sta_info->mode = SLSI_80211_MODE_11B;
	KUNIT_EXPECT_EQ(test, 0, slsi_fill_ap_sta_info(sdev, dev, peer_mac, ap_sta_info, 0));

	kfree_skb(ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]->assoc_ie);
}

#ifdef CONFIG_SCSC_WLAN_EHT
static void test_mgt_slsi_ml_get_sta_bss(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_FALSE(test, slsi_ml_get_sta_bss(sdev, dev));
}
#endif

static void test_mgt_slsi_retry_connection(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	struct slsi_ssid_info   *ssid_info = kunit_kzalloc(test, sizeof(*ssid_info), GFP_KERNEL);
	struct slsi_bssid_info *bssid_info = kunit_kzalloc(test, sizeof(*bssid_info), GFP_KERNEL);

	INIT_LIST_HEAD(&ndev_vif->sta.ssid_info);
	list_add(&ssid_info->list, &ndev_vif->sta.ssid_info);
	ssid_info->ssid.ssid_len = 10;
	memcpy(ssid_info->ssid.ssid, "test_ssid", ssid_info->ssid.ssid_len);
	ndev_vif->sta.ssid_len = ssid_info->ssid.ssid_len;
	memcpy(ndev_vif->sta.ssid, "test_ssid", ndev_vif->sta.ssid_len);
	ssid_info->akm_type = SLSI_BSS_SECURED_PSK;
	ndev_vif->sta.akm_type = ssid_info->akm_type;
	INIT_LIST_HEAD(&ssid_info->bssid_list);
	INIT_LIST_HEAD(&ndev_vif->acl_data_fw_list);
	list_add(&bssid_info->list, &ssid_info->bssid_list);
	bssid_info->connect_attempted = false;
	SLSI_ETHER_COPY(bssid_info->bssid, SLSI_DEFAULT_HW_MAC_ADDR);
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]->valid = true;

	ndev_vif->chan = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	ndev_vif->sta.sme.channel = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	ndev_vif->sta.sme.channel->center_freq = 0;

	sdev->wiphy->bands[NL80211_BAND_5GHZ] = kunit_kzalloc(test, sizeof(struct ieee80211_supported_band),
							      GFP_KERNEL);
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->n_channels = 1;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels = kunit_kzalloc(test, sizeof(struct ieee80211_channel),
									GFP_KERNEL);
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels[0].center_freq = 0;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels[0].freq_offset = 0;
	KUNIT_EXPECT_EQ(test, 1, slsi_retry_connection(sdev, dev));

#ifdef CONFIG_SCSC_WLAN_EHT
	ndev_vif->sta.ml_connection = true;
	ndev_vif->sta.max_ml_link = MAX_SUPP_MLO_LINKS;
	ndev_vif->sta.connecting_links = MAX_SUPP_MLO_LINKS;
	KUNIT_EXPECT_EQ(test, 0, slsi_retry_connection(sdev, dev));
#endif
}

static void test_mgt_slsi_free_connection_params(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);

	slsi_free_connection_params(sdev, dev);
}

static void test_mgt_slsi_handle_disconnect(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	u8                      *mac = SLSI_DEFAULT_HW_MAC_ADDR;
	u16                     reason = FAPI_REASONCODE_SYNCHRONISATION_LOSS;

	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	ndev_vif->activated = true;
	ndev_vif->sta.drv_connect_req_ongoing = true;
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_CONNECTING;
	ndev_vif->sta.sme.channel = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	SLSI_MUTEX_INIT(sdev->device_config_mutex);
	SLSI_MUTEX_INIT(ndev_vif->scan_mutex);
	ndev_vif->wlan_wl_sae.ws = kunit_kzalloc(test, sizeof(struct wakeup_source), GFP_KERNEL);
	wakeup_source_add(ndev_vif->wlan_wl_sae.ws);
	INIT_LIST_HEAD(&ndev_vif->sta.network_map);

	ndev_vif->vif_type = FAPI_VIFTYPE_AP;
	SLSI_MUTEX_INIT(sdev->tspec_mutex);
	memset(ndev_vif->sta.keepalive_host_tag, 0, sizeof(ndev_vif->sta.keepalive_host_tag));
	ndev_vif->sta.sta_bss = kunit_kzalloc(test, sizeof(struct cfg80211_bss), GFP_KERNEL);
	INIT_LIST_HEAD(&ndev_vif->sta.tdls_candidate_setup_list);
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]->valid = false;
	KUNIT_EXPECT_EQ(test, 0, slsi_handle_disconnect(sdev, dev, mac, reason, NULL, 0
#ifdef CONFIG_SCSC_WLAN_EHT
			   , 0
#endif
			   ));

	ndev_vif->vif_type = FAPI_VIFTYPE_AP;
	ndev_vif->activated = true;
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]->valid = true;
	memcpy(ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]->address, mac, 6);
	KUNIT_EXPECT_EQ(test, 0, slsi_handle_disconnect(sdev, dev, mac, reason, NULL, 0
#ifdef CONFIG_SCSC_WLAN_EHT
			   , 0
#endif
			   ));
}

static void test_mgt_slsi_ps_port_control(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct slsi_peer        *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);

	KUNIT_EXPECT_EQ(test, 0, slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_DISCONNECTED));
	KUNIT_EXPECT_EQ(test, 0, slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_DOING_KEY_CONFIG));
	KUNIT_EXPECT_EQ(test, 0, slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_CONNECTED));
	KUNIT_EXPECT_EQ(test, 0, slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_CONNECTING));
}

static void test_mgt_slsi_set_uint_mib(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_set_uint_mib(sdev, dev, 0, 0));
}

static void test_mgt_slsi_send_max_transmit_msdu_lifetime(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	u32                     msdu_lifetime = 0;

	KUNIT_EXPECT_EQ(test, 0, slsi_send_max_transmit_msdu_lifetime(sdev, dev, msdu_lifetime));
}

static void test_mgt_slsi_read_max_transmit_msdu_lifetime(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	u32                     msdu_lifetime;

	KUNIT_EXPECT_EQ(test, 0, slsi_read_max_transmit_msdu_lifetime(sdev, dev, &msdu_lifetime));
}

#ifdef CONFIG_CFG80211_CRDA_SUPPORT
static void test_mgt_slsi_update_custom_regulatory_orig_flags(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);

	slsi_update_custom_regulatory_orig_flags(sdev);
}
#endif

static void test_mgt_slsi_band_cfg_update(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);

	slsi_band_cfg_update(sdev, SLSI_FREQ_BAND_AUTO);
	slsi_band_cfg_update(sdev, SLSI_FREQ_BAND_5GHZ);
	slsi_band_cfg_update(sdev, SLSI_FREQ_BAND_2GHZ);
#ifdef CONFIG_SCSC_WLAN_SUPPORT_6G
	slsi_band_cfg_update(sdev, SLSI_FREQ_BAND_2_4GHZ_5GHZ_6GHZ);
	slsi_band_cfg_update(sdev, SLSI_FREQ_BAND_6GHZ);
	slsi_band_cfg_update(sdev, SLSI_FREQ_BAND_5GHZ_6GHZ);
	slsi_band_cfg_update(sdev, SLSI_FREQ_BAND_2_4GHZ_6GHZ);
#endif
}

static void test_mgt_slsi_band_update(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);

	sdev->device_config.supported_band = SLSI_FREQ_BAND_2GHZ;
	KUNIT_EXPECT_EQ(test, 0, slsi_band_update(sdev, SLSI_FREQ_BAND_2GHZ));

	KUNIT_EXPECT_EQ(test, 0, slsi_band_update(sdev, SLSI_FREQ_BAND_AUTO));

	sdev->netdev[1] = dev;
	KUNIT_EXPECT_EQ(test, 0, slsi_band_update(sdev, SLSI_FREQ_BAND_5GHZ));
}

static void test_mgt_slsi_disconnect_on_band_update(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	int                     new_band = SLSI_FREQ_BAND_5GHZ;

	sdev->recovery_timeout = 9;
	ndev_vif->activated = true;
	ndev_vif->vif_type = FAPI_VIFTYPE_NAN;
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_CONNECTED;
	ndev_vif->chan = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	ndev_vif->chan->band = NL80211_BAND_2GHZ;
	INIT_LIST_HEAD(&ndev_vif->sta.network_map);
	INIT_LIST_HEAD(&ndev_vif->sta.tdls_candidate_setup_list);

	KUNIT_EXPECT_EQ(test, 0, slsi_disconnect_on_band_update(sdev, dev, new_band));
}

static void test_mgt_slsi_send_gratuitous_arp(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	ndev_vif->ipaddress = cpu_to_be32(1);
	ndev_vif->activated = true;
	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_CONNECTED;
	dev->dev_addr = SLSI_DEFAULT_HW_MAC_ADDR;
	KUNIT_EXPECT_EQ(test, 0, slsi_send_gratuitous_arp(sdev, dev));
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

static void test_mgt_slsi_create_packet_filter_element(struct kunit *test)
{
	struct slsi_mlme_pkt_filter_elem pkt_filter_elem[2];
	struct slsi_mlme_pattern_desc    pattern_desc[2];
	int                              num_pattern_desc = 2;
	int                              pkt_filters_len;

	slsi_create_packet_filter_element(0, 0, num_pattern_desc, pattern_desc,
					  pkt_filter_elem, &pkt_filters_len);
}

static void test_mgt_slsi_set_common_packet_filters(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_set_common_packet_filters(sdev, dev));
}

static void test_mgt_slsi_set_arp_packet_filter(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	struct slsi_peer        *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);

	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = peer;
	peer->valid = true;
	peer->assoc_resp_ie = kunit_kzalloc(test, sizeof(u8), GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 0, slsi_set_arp_packet_filter(sdev, dev));
}

#ifdef CONFIG_SCSC_WLAN_ENHANCED_PKT_FILTER
static void test_mgt_slsi_set_enhanced_pkt_filter(struct kunit *test)
{
	struct net_device       *dev = TEST_TO_DEV(test);
	char                    *command = "ENHANCED_PKT_FILTER 1 2";

	KUNIT_EXPECT_EQ(test, 0, slsi_set_enhanced_pkt_filter(dev, command, sizeof(command)));
}

static void test_mgt_slsi_set_opt_out_unicast_packet_filter(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_set_opt_out_unicast_packet_filter(sdev, dev));
}

static void test_mgt_slsi_set_opt_in_tcp4_packet_filter(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_set_opt_in_tcp4_packet_filter(sdev, dev));
}

static void test_mgt_slsi_set_opt_in_tcp6_packet_filter(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_set_opt_in_tcp6_packet_filter(sdev, dev));
}
#endif

static void test_mgt_slsi_set_multicast_packet_filters(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);

	dev->dev_addr = SLSI_DEFAULT_HW_MAC_ADDR;
	KUNIT_EXPECT_EQ(test, 0, slsi_set_multicast_packet_filters(sdev, dev));
}

static void test_mgt_slsi_clear_packet_filters(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	struct slsi_peer        *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);

	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = peer;
	peer->valid = true;
	peer->assoc_resp_ie = kunit_kzalloc(test, sizeof(u8), GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 0, slsi_clear_packet_filters(sdev, dev));
}

static void test_mgt_slsi_update_packet_filters(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	struct slsi_peer        *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);

	dev->dev_addr = SLSI_DEFAULT_HW_MAC_ADDR;
	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = peer;
	peer->valid = true;
	peer->assoc_resp_ie = kunit_kzalloc(test, sizeof(u8), GFP_KERNEL);
#ifdef CONFIG_SCSC_WLAN_ENHANCED_PKT_FILTER
	sdev->enhanced_pkt_filter_enabled = true;
#endif
	KUNIT_EXPECT_EQ(test, 0, slsi_update_packet_filters(sdev, dev));
}

static void test_mgt_slsi_set_packet_filters(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	struct slsi_peer        *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);

	ndev_vif->activated = true;
	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = peer;
	peer->assoc_resp_ie = alloc_skb(100, GFP_KERNEL);

	slsi_set_packet_filters(sdev, dev);
	kfree_skb(peer->assoc_resp_ie);
}

static void test_mgt_slsi_ip_address_changed(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);
	struct slsi_peer        *peer = kunit_kzalloc(test, sizeof(*peer), GFP_KERNEL);
	u32                     ipaddress = 1;

	ndev_vif->activated = true;
	ndev_vif->vif_type = FAPI_VIFTYPE_AP;
	KUNIT_EXPECT_EQ(test, 0, slsi_ip_address_changed(sdev, dev, ipaddress));

	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;
	ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = peer;
	peer->valid = true;
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_CONNECTED;
	dev->dev_addr = SLSI_DEFAULT_HW_MAC_ADDR;
	KUNIT_EXPECT_EQ(test, 0, slsi_ip_address_changed(sdev, dev, ipaddress));
}

static void test_mgt_slsi_auto_chan_select_scan(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct sk_buff           *skb = fapi_alloc(mlme_scan_ind, MLME_SCAN_IND, 0, 100);
	struct ieee80211_channel *channels[SLSI_AP_AUTO_CHANLS_LIST_FROM_HOSTAPD_MAX];
	int                      i = 0;

	ndev_vif->scan[SLSI_SCAN_HW_ID].scan_results = kmalloc(sizeof(struct slsi_scan_result), GFP_KERNEL);
	ndev_vif->scan[SLSI_SCAN_HW_ID].scan_results->beacon = NULL;
	ndev_vif->scan[SLSI_SCAN_HW_ID].scan_results->probe_resp = skb;
	ndev_vif->scan[SLSI_SCAN_HW_ID].scan_results->next = NULL;
	sdev->netdev[SLSI_NET_INDEX_WLAN] = dev;
	for (i = 0; i < SLSI_AP_AUTO_CHANLS_LIST_FROM_HOSTAPD_MAX; i++)
		channels[i] = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 0, slsi_auto_chan_select_scan(sdev, 0, channels));

	kfree_skb(skb);
}

static void test_mgt_slsi_set_boost(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_set_boost(sdev, dev));
}

static void test_mgt_slsi_p2p_roc_duration_expiry_work(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);

	ndev_vif->chan = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	slsi_p2p_roc_duration_expiry_work(&ndev_vif->unsync.roc_expiry_work);
}

static void test_mgt_slsi_p2p_unsync_vif_delete_work(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);

	slsi_p2p_unsync_vif_delete_work(&ndev_vif->unsync.del_vif_work);
}

static void test_mgt_slsi_p2p_unset_channel_expiry_work(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);

	slsi_p2p_unset_channel_expiry_work(&ndev_vif->unsync.unset_channel_expiry_work);
}

static void test_mgt_slsi_p2p_init(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);

	KUNIT_EXPECT_EQ(test, 0, slsi_p2p_init(sdev, ndev_vif));
}

static void test_mgt_slsi_p2p_deinit(struct kunit *test)
{
	struct slsi_dev         *sdev = TEST_TO_SDEV(test);
	struct net_device       *dev = TEST_TO_DEV(test);
	struct netdev_vif       *ndev_vif = netdev_priv(dev);

	slsi_p2p_deinit(sdev, ndev_vif);
}

static void test_mgt_slsi_p2p_vif_activate(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct ieee80211_channel *chan = kunit_kzalloc(test, sizeof(*chan), GFP_KERNEL);

	sdev->p2p_state = P2P_IDLE_NO_VIF;
	ndev_vif->unsync.probe_rsp_ies_len = 10;
	ndev_vif->unsync.probe_rsp_ies = kmalloc(ndev_vif->unsync.probe_rsp_ies_len, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 0, slsi_p2p_vif_activate(sdev, dev, chan, 0, true));

	kfree(ndev_vif->unsync.probe_rsp_ies);
}

static void test_mgt_slsi_p2p_vif_deactivate(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	ndev_vif->mgmt_tx_data.exp_frame = SLSI_PA_GAS_INITIAL_REQ;
	ndev_vif->mgmt_tx_data.host_tag = 1;
	ndev_vif->unsync.probe_rsp_ies_len = 0;
	sdev->p2p_state = P2P_IDLE_VIF_ACTIVE;

	slsi_p2p_vif_deactivate(sdev, dev, true);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

static void test_mgt_slsi_p2p_group_start_remove_unsync_vif(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);

	sdev->netdev[SLSI_NET_INDEX_P2P] = dev;
	ndev_vif->ifnum = SLSI_NET_INDEX_P2P;
	ndev_vif->vif_type = FAPI_VIFTYPE_DISCOVERY;

	slsi_p2p_group_start_remove_unsync_vif(sdev);
}

static void test_mgt_slsi_p2p_dev_probe_rsp_ie(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	size_t                   probe_rsp_ie_len = 10;
	u8                       *probe_rsp_ie;

	ndev_vif->ifnum = SLSI_NET_INDEX_P2P;
	ndev_vif->vif_type = FAPI_VIFTYPE_DISCOVERY;
	ndev_vif->unsync.listen_offload = true;
	ndev_vif->unsync.probe_rsp_ies = kmalloc(probe_rsp_ie_len, GFP_KERNEL);
	ndev_vif->unsync.probe_rsp_ies_len = probe_rsp_ie_len;
	probe_rsp_ie = kmalloc(probe_rsp_ie_len, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 0, slsi_p2p_dev_probe_rsp_ie(sdev, dev, probe_rsp_ie, probe_rsp_ie_len));

	ndev_vif->unsync.listen_offload = false;
	sdev->p2p_state = P2P_LISTENING;
	probe_rsp_ie = kmalloc(probe_rsp_ie_len, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 0, slsi_p2p_dev_probe_rsp_ie(sdev, dev, probe_rsp_ie, probe_rsp_ie_len));
	kfree(ndev_vif->unsync.probe_rsp_ies);
}

static void test_mgt_slsi_p2p_dev_null_ies(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);

	sdev->p2p_state = P2P_SCANNING;
	ndev_vif->ifnum = SLSI_NET_INDEX_P2P;
	ndev_vif->vif_type = FAPI_VIFTYPE_DISCOVERY;
	ndev_vif->activated = true;
	ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req = kunit_kzalloc(test,
								 sizeof(struct cfg80211_scan_request),
								 GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 0, slsi_p2p_dev_null_ies(sdev, dev));
}

static void test_mgt_slsi_get_public_action_subtype(struct kunit *test)
{
	struct ieee80211_mgmt *mgmt = kunit_kzalloc(test, sizeof(*mgmt), GFP_KERNEL);

	((u8 *)&mgmt->u.action.u)[0] = SLSI_PA_GAS_COMEBACK_RSP;
	((u8 *)&mgmt->u.action.u)[1] = 0x50;
	((u8 *)&mgmt->u.action.u)[2] = 0x6f;
	((u8 *)&mgmt->u.action.u)[3] = 0x9a;
	((u8 *)&mgmt->u.action.u)[4] = 0x09;
	((u8 *)&mgmt->u.action.u)[5] = SLSI_PA_INVALID;
	KUNIT_EXPECT_EQ(test,
			SLSI_PA_GAS_COMEBACK_RSP | SLSI_PA_GAS_DUMMY_SUBTYPE_MASK,
			slsi_get_public_action_subtype(mgmt));
}

static void test_mgt_slsi_p2p_get_action_frame_status(struct kunit *test)
{
	struct net_device     *dev = TEST_TO_DEV(test);
	struct ieee80211_mgmt *mgmt = kunit_kzalloc(test, sizeof(*mgmt), GFP_KERNEL);
	u8                    action[13] = {SLSI_PA_GAS_COMEBACK_RSP,
					   0x50, 0x6f, 0x9a, 0x09,
					   SLSI_PA_INVALID,
					   SLSI_PA_INVALID,
					   SLSI_WLAN_EID_VENDOR_SPECIFIC,
					   SLSI_PA_INVALID,
					   0x50, 0x6f, 0x9a, 0x09};

	KUNIT_EXPECT_EQ(test, 0, slsi_p2p_get_action_frame_status(dev, mgmt));
}

static void test_mgt_slsi_get_exp_peer_frame_subtype(struct kunit *test)
{
	u8 subtype = SLSI_PA_INVALID;

	KUNIT_EXPECT_EQ(test, SLSI_PA_INVALID, slsi_get_exp_peer_frame_subtype(subtype));
}

#if !(defined(SCSC_SEP_VERSION) && SCSC_SEP_VERSION < 11) || defined(CONFIG_SCSC_WLAN_SUPPORT_6G)
static void test_mgt_slsi_bss_connect_type_get(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	u8                       ie[10];
	u8                       ie_type;

	KUNIT_EXPECT_EQ(test, 0, slsi_bss_connect_type_get(sdev, ie, sizeof(ie), &ie_type));
}
#endif

static void test_mgt_slsi_wlan_dump_public_action_subtype(struct kunit *test)
{
	struct slsi_dev       *sdev = TEST_TO_SDEV(test);
	struct ieee80211_mgmt *mgmt = kunit_kzalloc(test, sizeof(*mgmt), GFP_KERNEL);

	mgmt->u.action.category = WLAN_CATEGORY_RADIO_MEASUREMENT;
	((u8 *)&mgmt->u.action.u)[0] = SLSI_PA_INVALID;
	slsi_wlan_dump_public_action_subtype(sdev, mgmt, true);

	mgmt->u.action.category = WLAN_CATEGORY_PUBLIC;
	slsi_wlan_dump_public_action_subtype(sdev, mgmt, true);

	mgmt->u.action.category = WLAN_CATEGORY_WNM;
	slsi_wlan_dump_public_action_subtype(sdev, mgmt, true);
}

static void test_mgt_slsi_abort_sta_scan(struct kunit *test)
{
	struct slsi_dev       *sdev = TEST_TO_SDEV(test);
	struct net_device     *dev = TEST_TO_DEV(test);
	struct netdev_vif     *ndev_vif = netdev_priv(dev);

	sdev->netdev[SLSI_NET_INDEX_WLAN] = dev;
	ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req = kunit_kzalloc(test,
								 sizeof(struct cfg80211_scan_request),
								 GFP_KERNEL);
	slsi_abort_sta_scan(sdev);
}

static void test_mgt_slsi_is_dhcp_packet(struct kunit *test)
{
	u8 data[100];

	data[SLSI_IP_TYPE_OFFSET] = SLSI_IP_TYPE_UDP;
	KUNIT_EXPECT_EQ(test, SLSI_TX_IS_NOT_DHCP, slsi_is_dhcp_packet(data));
}

#ifdef CONFIG_SCSC_WLAN_PRIORITISE_IMP_FRAMES
static void test_mgt_slsi_is_tcp_sync_packet(struct kunit *test)
{
	struct net_device     *dev = TEST_TO_DEV(test);
	struct netdev_vif     *ndev_vif = netdev_priv(dev);
	struct sk_buff        *skb = alloc_skb(100, GFP_KERNEL);
	struct ethhdr         *ehdr = eth_hdr(skb);

	ndev_vif->vif_type = FAPI_VIFTYPE_AP;
	ehdr->h_proto = cpu_to_be16(ETH_P_IP);
	ip_hdr(skb)->protocol = IPPROTO_TCP;
	tcp_hdr(skb)->syn = 0;

	KUNIT_EXPECT_EQ(test, 0, slsi_is_tcp_sync_packet(dev, skb));
	kfree_skb(skb);
}

static void test_mgt_slsi_is_dns_packet(struct kunit *test)
{
	u8 data[100];

	data[SLSI_IP_TYPE_OFFSET] = SLSI_IP_TYPE_UDP;
	KUNIT_EXPECT_EQ(test, 0, slsi_is_dns_packet(data));
}

static void test_mgt_slsi_is_mdns_packet(struct kunit *test)
{
	u8 data[100];

	data[SLSI_IP_TYPE_OFFSET] = SLSI_IP_TYPE_UDP;
	KUNIT_EXPECT_EQ(test, 0, slsi_is_mdns_packet(data));
}
#endif

static void test_mgt_slsi_ap_prepare_add_info_ies(struct kunit *test)
{
	struct net_device     *dev = TEST_TO_DEV(test);
	struct netdev_vif     *ndev_vif = netdev_priv(dev);
	u8                    ies = 0;
	size_t                ies_len = 10;

	KUNIT_EXPECT_EQ(test, 0, slsi_ap_prepare_add_info_ies(ndev_vif, &ies, ies_len));
}

static void test_mgt_slsi_get_channel_jiffies_index(struct kunit *test)
{
	enum nl80211_band band = NL80211_BAND_2GHZ;
	int               channel = 11;

	KUNIT_EXPECT_EQ(test, 11, slsi_get_channel_jiffies_index(channel, band));

	band = NL80211_BAND_5GHZ;
	channel = 40;
	KUNIT_EXPECT_EQ(test, 16, slsi_get_channel_jiffies_index(channel, band));

	channel = 100;
	KUNIT_EXPECT_EQ(test, 23, slsi_get_channel_jiffies_index(channel, band));

	channel = 149;
	KUNIT_EXPECT_EQ(test, 34, slsi_get_channel_jiffies_index(channel, band));

#ifdef CONFIG_SCSC_WLAN_SUPPORT_6G
	band = NL80211_BAND_6GHZ;
	channel = 2;
	KUNIT_EXPECT_EQ(test,
			SLSI_NUM_2P4GHZ_CHANNELS + SLSI_NUM_5GHZ_CHANNELS + 1,
			slsi_get_channel_jiffies_index(channel, band));

	channel = 1;
	KUNIT_EXPECT_NE(test,
			0,
			slsi_get_channel_jiffies_index(channel, band));
#endif
}

static void test_mgt_slsi_roam_channel_cache_add_channel(struct kunit *test)
{
	struct slsi_roaming_network_map_entry *network_map = kunit_kzalloc(test,
							     sizeof(*network_map),
							     GFP_KERNEL);
	u8                                    channel = 11;
	enum nl80211_band                     band = NL80211_BAND_2GHZ;
	bool                                  is_6ghz_support = true;

	slsi_roam_channel_cache_add_channel(network_map, channel, band, is_6ghz_support);

	band = NL80211_BAND_5GHZ;
	channel = 40;
	slsi_roam_channel_cache_add_channel(network_map, channel, band, is_6ghz_support);

	channel = 100;
	slsi_roam_channel_cache_add_channel(network_map, channel, band, is_6ghz_support);

	channel = 149;
	slsi_roam_channel_cache_add_channel(network_map, channel, band, is_6ghz_support);

#ifdef CONFIG_SCSC_WLAN_SUPPORT_6G
	band = NL80211_BAND_6GHZ;
	channel = 2;
	slsi_roam_channel_cache_add_channel(network_map, channel, band, is_6ghz_support);

	channel = 1;
	slsi_roam_channel_cache_add_channel(network_map, channel, band, is_6ghz_support);

	channel = 181;
	slsi_roam_channel_cache_add_channel(network_map, channel, band, is_6ghz_support);
#endif
}

static void test_mgt_slsi_roam_channel_cache_add_entry(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	char              *ssid = "test_ssid";
	u8                *bssid = SLSI_DEFAULT_HW_MAC_ADDR;
	u8                ssid_len = strlen(ssid) + 1;
	u8                channel = 11;
	enum nl80211_band band = NL80211_BAND_2GHZ;
	struct slsi_roaming_network_map_entry *network_map = kunit_kzalloc(test,
									   sizeof(*network_map),
									   GFP_KERNEL);

	INIT_LIST_HEAD(&ndev_vif->sta.network_map);
	slsi_roam_channel_cache_add_entry(sdev, dev, (u8 *)ssid, ssid_len, bssid, channel, band);
	network_map->ssid.ssid_len = strlen(ssid) + 1;
	memcpy(network_map->ssid.ssid, "test_ssid", network_map->ssid.ssid_len);
	list_add(&network_map->list, &ndev_vif->sta.network_map);
	slsi_roam_channel_cache_add_entry(sdev, dev, (u8 *)ssid, ssid_len, bssid, channel, band);
}

static void test_mgt_slsi_roam_channel_cache_add(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *skb = fapi_alloc(mlme_scan_ind, MLME_SCAN_IND, 0, 128);

	INIT_LIST_HEAD(&ndev_vif->sta.network_map);
	fapi_set_u16(skb, u.mlme_scan_ind.channel_frequency, 2462);
	slsi_roam_channel_cache_add(sdev, dev, skb);

	fapi_set_u16(skb, u.mlme_scan_ind.channel_frequency, 5745);
	slsi_roam_channel_cache_add(sdev, dev, skb);
	kfree_skb(skb);
}

static void test_mgt_slsi_roam_channel_cache_prune(struct kunit *test)
{
	struct slsi_dev                       *sdev = TEST_TO_SDEV(test);
	struct net_device                     *dev = TEST_TO_DEV(test);
	struct netdev_vif                     *ndev_vif = netdev_priv(dev);
	struct slsi_roaming_network_map_entry *network_map = kunit_kzalloc(test,
									   sizeof(*network_map),
									   GFP_KERNEL);
	char                                  *ssid = "test_ssid";

	INIT_LIST_HEAD(&ndev_vif->sta.network_map);
	INIT_LIST_HEAD(&network_map->list);
	list_add(&network_map->list, &ndev_vif->sta.network_map);
	sdev->band_6g_supported = true;
	slsi_roam_channel_cache_prune(dev, 1, ssid, strlen(ssid));

	INIT_LIST_HEAD(&ndev_vif->sta.network_map);
	INIT_LIST_HEAD(&network_map->list);
	list_add(&network_map->list, &ndev_vif->sta.network_map);
	sdev->band_6g_supported = false;
	slsi_roam_channel_cache_prune(dev, 1, ssid, strlen(ssid));

	INIT_LIST_HEAD(&ndev_vif->sta.network_map);
	INIT_LIST_HEAD(&network_map->list);
	list_add(&network_map->list, &ndev_vif->sta.network_map);
	slsi_roam_channel_cache_prune(dev, 1, ssid, strlen(ssid));
}

static void test_mgt_slsi_roam_channel_cache_get_channels_int(struct kunit *test)
{
	struct slsi_dev                       *sdev = TEST_TO_SDEV(test);
	struct net_device                     *dev = TEST_TO_DEV(test);
	struct slsi_roaming_network_map_entry *network_map = kunit_kzalloc(test,
									   sizeof(*network_map),
									   GFP_KERNEL);
	u16                                   channels[SLSI_ROAMING_CHANNELS_MAX];

	network_map->channels_24_ghz = 0x02;
	KUNIT_EXPECT_EQ(test, 1, slsi_roam_channel_cache_get_channels_int(dev, network_map, channels));

	network_map->channels_5_ghz = 0x01;
	KUNIT_EXPECT_EQ(test, 2, slsi_roam_channel_cache_get_channels_int(dev, network_map, channels));

	network_map->channels_5_ghz = 0x04;
	KUNIT_EXPECT_EQ(test, 2, slsi_roam_channel_cache_get_channels_int(dev, network_map, channels));

	network_map->channels_5_ghz = 0x40;
	KUNIT_EXPECT_EQ(test, 2, slsi_roam_channel_cache_get_channels_int(dev, network_map, channels));

	network_map->channels_5_ghz = 0x40;
	sdev->band_6g_supported = true;
	network_map->channels_6_ghz.unii5_6 = 0xB2B7;
	KUNIT_EXPECT_EQ(test, 12, slsi_roam_channel_cache_get_channels_int(dev, network_map, channels));
}

static void test_mgt_slsi_roam_channel_cache_get(struct kunit *test)
{
	struct net_device                     *dev = TEST_TO_DEV(test);
	struct netdev_vif                     *ndev_vif = netdev_priv(dev);
	struct slsi_roaming_network_map_entry *network_map = kunit_kzalloc(test,
							     sizeof(*network_map),
							     GFP_KERNEL);
	u8                                    ssid[12] = {0};

	ssid[1] = 10;
	memcpy(&ssid[2], "test_ssid", ssid[1]);
	memcpy(network_map->ssid.ssid, "test_ssid", ssid[1]);
	network_map->ssid.ssid_len = ssid[1];
	INIT_LIST_HEAD(&ndev_vif->sta.network_map);
	list_add(&network_map->list, &ndev_vif->sta.network_map);
	KUNIT_EXPECT_PTR_EQ(test, network_map, slsi_roam_channel_cache_get(dev, ssid));
}

static void test_mgt_slsi_roam_channel_cache_get_channels(struct kunit *test)
{
	struct net_device                     *dev = TEST_TO_DEV(test);
	struct netdev_vif                     *ndev_vif = netdev_priv(dev);
	struct slsi_roaming_network_map_entry *network_map = kunit_kzalloc(test,
							     sizeof(*network_map),
							     GFP_KERNEL);
	u8                                    ssid[12] = {0};
	u16                                   channels[SLSI_ROAMING_CHANNELS_MAX];

	ssid[1] = 10;
	memcpy(&ssid[2], "test_ssid", ssid[1]);
	memcpy(network_map->ssid.ssid, "test_ssid", ssid[1]);
	network_map->ssid.ssid_len = ssid[1];
	INIT_LIST_HEAD(&ndev_vif->sta.network_map);
	list_add(&network_map->list, &ndev_vif->sta.network_map);
	KUNIT_EXPECT_EQ(test, 0, slsi_roam_channel_cache_get_channels(dev, ssid, channels));
}

static void test_mgt_slsi_roaming_scan_configure_channels(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	ndev_vif->activated = true;
	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;

	KUNIT_EXPECT_EQ(test, 0, slsi_roaming_scan_configure_channels(sdev, dev, NULL, NULL));
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

static void test_mgt_slsi_send_rcl_event(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	u16               channel_list[MAX_CHANNEL_COUNT] = {0};
	u8                ssid[12] = {0};

	ssid[1] = 10;
	memcpy(&ssid[2], "test_ssid", ssid[1]);
	sdev->wiphy->n_vendor_events = SLSI_NL80211_VENDOR_RCL_EVENT + 1;
	KUNIT_EXPECT_EQ(test, 0, slsi_send_rcl_event(sdev, 1, channel_list, ssid, 10));
}

static void test_mgt_slsi_send_acs_event(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct net_device *dev = TEST_TO_DEV(test);
	struct slsi_acs_selected_channels acs_selected_channels;

	sdev->wiphy->n_vendor_events = SLSI_NL80211_VENDOR_ACS_EVENT + 1;
	KUNIT_EXPECT_EQ(test, 0, slsi_send_acs_event(sdev, dev, acs_selected_channels));
}

#ifdef CONFIG_SCSC_WLAN_WES_NCHO
static void test_mgt_slsi_is_wes_action_frame(struct kunit *test)
{
	struct ieee80211_mgmt *mgmt = kunit_kzalloc(test,
						    sizeof(struct ieee80211_mgmt),
						    GFP_KERNEL);

	((u8 *)&mgmt->u.action.u)[0] = 0x7f;
	((u8 *)&mgmt->u.action.u)[1] = 0x00;
	((u8 *)&mgmt->u.action.u)[2] = 0x00;
	((u8 *)&mgmt->u.action.u)[3] = 0xf0;

	KUNIT_EXPECT_EQ(test, 0, slsi_is_wes_action_frame(mgmt));
}
#endif

static void test_mgt_slsi_remap_reg_rule_flags(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, NL80211_RRF_DFS, slsi_remap_reg_rule_flags(SLSI_REGULATORY_DFS));
	KUNIT_EXPECT_EQ(test, NL80211_RRF_NO_OFDM, slsi_remap_reg_rule_flags(SLSI_REGULATORY_NO_OFDM));
	KUNIT_EXPECT_EQ(test, NL80211_RRF_NO_INDOOR, slsi_remap_reg_rule_flags(SLSI_REGULATORY_NO_INDOOR));
	KUNIT_EXPECT_EQ(test, NL80211_RRF_NO_OUTDOOR, slsi_remap_reg_rule_flags(SLSI_REGULATORY_NO_OUTDOOR));
	KUNIT_EXPECT_EQ(test, NL80211_RRF_AUTO_BW, slsi_remap_reg_rule_flags(SLSI_REGULATORY_AUTO_BW));
	KUNIT_EXPECT_EQ(test, NL80211_RRF_NO_IR, slsi_remap_reg_rule_flags(SLSI_REGULATORY_NO_IR));
}

static void test_mgt_slsi_reset_channel_flags(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);

	sdev->wiphy->bands[NL80211_BAND_5GHZ] = kunit_kzalloc(test,
							      sizeof(struct ieee80211_supported_band),
							      GFP_KERNEL);

	sdev->wiphy->bands[NL80211_BAND_5GHZ]->n_channels = 1;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels = kunit_kzalloc(test,
									sizeof(struct ieee80211_channel),
									GFP_KERNEL);

	slsi_reset_channel_flags(sdev);
}

static void test_mgt_slsi_read_regulatory_rules(struct kunit *test)
{
	struct slsi_dev                *sdev = TEST_TO_SDEV(test);
	struct net_device              *dev = TEST_TO_DEV(test);
	struct slsi_802_11d_reg_domain domain_info;
	struct regdb_file_reg_country  country[1];
	char                           alpha2[2] = {0};

	memset(&domain_info, 0, sizeof(domain_info));
	domain_info.regdomain = kunit_kzalloc(test,
					      sizeof(struct ieee80211_regdomain) + sizeof(struct ieee80211_reg_rule),
					      GFP_KERNEL);
	sdev->regdb.regdb_state = SLSI_REG_DB_NOT_SET;
	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_read_regulatory_rules(sdev, &domain_info, alpha2));

	sdev->regdb.regdb_state = SLSI_REG_DB_SET;
	sdev->regdb.num_countries = 1;
	sdev->regdb.country = country;
	sdev->regdb.country[0].alpha2[0] = alpha2[0];
	sdev->regdb.country[0].alpha2[1] = alpha2[1];
	sdev->regdb.country[0].collection = kunit_kzalloc(test,
							  sizeof(struct regdb_file_reg_rules_collection),
							  GFP_KERNEL);
	sdev->regdb.country[0].collection->reg_rule_num = 1;
	sdev->regdb.country[0].collection->reg_rule[0] = kunit_kzalloc(test,
								       sizeof(struct regdb_file_reg_rule),
								       GFP_KERNEL);
	sdev->regdb.country[0].collection->reg_rule[0]->freq_range = kunit_kzalloc(test,
										   sizeof(struct regdb_file_freq_range),
										   GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 0, slsi_read_regulatory_rules(sdev, &domain_info, alpha2));
}

static void test_mgt_slsi_set_mib_rssi_boost(struct kunit *test)
{
	struct slsi_dev                *sdev = TEST_TO_SDEV(test);
	struct net_device              *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_set_mib_rssi_boost(sdev, dev, SLSI_PSID_UNIFI_ROAM_RSSI_BOOST, 1, 0));
}

#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING_CSA_LEGACY
static void test_mgt_slsi_if_valid_wifi_sharing_channel(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);

	KUNIT_EXPECT_EQ(test, 1, slsi_if_valid_wifi_sharing_channel(sdev, 5220));
}

static void test_mgt_slsi_check_if_non_indoor_non_dfs_channel(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);

	sdev->wiphy->bands[NL80211_BAND_5GHZ] = kunit_kzalloc(test,
							      sizeof(struct ieee80211_supported_band),
							      GFP_KERNEL);
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->n_channels = 1;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels = kunit_kzalloc(test,
									sizeof(struct ieee80211_channel),
									GFP_KERNEL);
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels->center_freq = 5220;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels->freq_offset = 0;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels->flags = (IEEE80211_CHAN_INDOOR_ONLY | IEEE80211_CHAN_RADAR |
								  IEEE80211_CHAN_DISABLED | IEEE80211_CHAN_NO_IR);

	slsi_check_if_non_indoor_non_dfs_channel(sdev, 5220);
}

static void test_mgt_slsi_get_mhs_ws_chan_vsdb(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);
	struct slsi_dev             *dev = TEST_TO_DEV(test);
	struct netdev_vif           *ndev_vif = netdev_priv(dev);
	struct cfg80211_ap_settings *settings = kunit_kzalloc(test, sizeof(*settings), GFP_KERNEL);
	int                         wifi_sharing_channel_switched;

	sdev->wiphy->bands[NL80211_BAND_5GHZ] = kunit_kzalloc(test,
							      sizeof(struct ieee80211_supported_band),
							      GFP_KERNEL);
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->n_channels = 1;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels = kunit_kzalloc(test,
									sizeof(struct ieee80211_channel),
									GFP_KERNEL);
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels->center_freq = 5220;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels->freq_offset = 0;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels->flags = (IEEE80211_CHAN_INDOOR_ONLY | IEEE80211_CHAN_RADAR |
								  IEEE80211_CHAN_DISABLED | IEEE80211_CHAN_NO_IR);

	sdev->netdev[SLSI_NET_INDEX_WLAN] = dev;
	ndev_vif->chan = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	ndev_vif->chan->center_freq = 5220;
	settings->chandef.chan = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	settings->chandef.chan->center_freq = 5745;

	KUNIT_EXPECT_EQ(test, 0, slsi_get_mhs_ws_chan_vsdb(sdev->wiphy, dev, settings, sdev,
							   &wifi_sharing_channel_switched));
}

static void test_mgt_slsi_get_mhs_ws_chan_rsdb(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);
	struct slsi_dev             *dev = TEST_TO_DEV(test);
	struct netdev_vif           *ndev_vif = netdev_priv(dev);
	struct cfg80211_ap_settings *settings = kunit_kzalloc(test, sizeof(*settings), GFP_KERNEL);
	int                         wifi_sharing_channel_switched;

	sdev->wiphy->bands[NL80211_BAND_5GHZ] = kunit_kzalloc(test,
							      sizeof(struct ieee80211_supported_band),
							      GFP_KERNEL);
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->n_channels = 1;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels = kunit_kzalloc(test,
									sizeof(struct ieee80211_channel),
									GFP_KERNEL);
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels->center_freq = 5220;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels->freq_offset = 0;
	sdev->wiphy->bands[NL80211_BAND_5GHZ]->channels->flags = (IEEE80211_CHAN_INDOOR_ONLY | IEEE80211_CHAN_RADAR |
								  IEEE80211_CHAN_DISABLED | IEEE80211_CHAN_NO_IR);

	sdev->netdev[SLSI_NET_INDEX_WLAN] = dev;
	ndev_vif->chan = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	ndev_vif->chan->band = NL80211_BAND_2GHZ;
	ndev_vif->chan->center_freq = 2462;
	settings->chandef.chan = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	settings->chandef.chan->center_freq = 5745;
	settings->chandef.chan->band = NL80211_BAND_5GHZ;

	KUNIT_EXPECT_EQ(test, 0, slsi_get_mhs_ws_chan_vsdb(sdev->wiphy, dev, settings, sdev,
							   &wifi_sharing_channel_switched));
}

static void test_mgt_slsi_check_if_channel_restricted_already(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);
	int                         channel = 5360;

	sdev->num_5g_restricted_channels = 1;
	sdev->wifi_sharing_5g_restricted_channels[0] = channel;
	KUNIT_EXPECT_EQ(test, 0, slsi_check_if_channel_restricted_already(sdev, channel));
}
#endif

#ifdef CONFIG_SCSC_WLAN_ENABLE_MAC_RANDOMISATION
static void test_mgt_slsi_set_mac_randomisation_mask(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);

	sdev->scan_addr_set = true;
	KUNIT_EXPECT_EQ(test, 0, slsi_set_mac_randomisation_mask(struct slsi_dev *sdev, SLSI_DEFAULT_HW_MAC_ADDR));
}
#endif

static void test_mgt__set_country_update_regd(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);
	char                        alpha2[4];

	sdev->device_config.domain_info.regdomain = kunit_kzalloc(test,
								  sizeof(struct ieee80211_regdomain),
								  GFP_KERNEL);
	memcpy(alpha2, sdev->device_config.domain_info.regdomain->alpha2, 4);
	sdev->regdb.regdb_state = SLSI_REG_DB_SET;
	sdev->regdb.num_countries = 0;
	sdev->regdb.country = kunit_kzalloc(test, sizeof(struct regdb_file_reg_country), GFP_KERNEL);
	sdev->regdb.country->collection = kunit_kzalloc(test,
							sizeof(struct regdb_file_reg_rules_collection),
							GFP_KERNEL);
	sdev->regdb.country->collection->reg_rule_num = 0;
	KUNIT_EXPECT_EQ(test, 0, __set_country_update_regd(sdev, alpha2, 4, true));
}

static void test_mgt_slsi_set_country_update_regd(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);
	char                        alpha2[4];

	sdev->device_config.domain_info.regdomain = kunit_kzalloc(test,
								  sizeof(struct ieee80211_regdomain),
								  GFP_KERNEL);
	memcpy(alpha2, sdev->device_config.domain_info.regdomain->alpha2, 3);
	KUNIT_EXPECT_EQ(test, 0, slsi_set_country_update_regd(sdev, alpha2, 3));
}

static void test_mgt_slsi_read_disconnect_ind_timeout(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);
	u16                         psid = SLSI_PSID_UNIFI_DISCONNECT_TIMEOUT;

	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_read_disconnect_ind_timeout(sdev, psid));
}

static void test_mgt_slsi_get_beacon_cu(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);
	struct net_device           *dev = TEST_TO_DEV(test);
	int                         mib_value;

	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_get_beacon_cu(sdev, dev, &mib_value, 0));
}

static void test_mgt_slsi_get_ps_disabled_duration(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);
	struct net_device           *dev = TEST_TO_DEV(test);
	int                         mib_value;

	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_get_ps_disabled_duration(sdev, dev, &mib_value));
}

static void test_mgt_slsi_get_ps_entry_counter(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);
	struct net_device           *dev = TEST_TO_DEV(test);
	int                         mib_value;

	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_get_ps_entry_counter(sdev, dev, &mib_value));
}

static void test_mgt_slsi_clear_offchannel_data(struct kunit *test)
{
	struct slsi_dev             *sdev = TEST_TO_SDEV(test);
	struct net_device           *dev = TEST_TO_DEV(test);

	sdev->netdev[SLSI_NET_INDEX_P2PX_SWLAN] = dev;
	slsi_clear_offchannel_data(sdev, true);
}

static void test_mgt_slsi_hs2_unsync_vif_delete_work(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	sdev->wlan_unsync_vif_state = WLAN_UNSYNC_NO_VIF;

	slsi_hs2_unsync_vif_delete_work(&ndev_vif->unsync.hs2_del_vif_work);
}

static void test_mgt_slsi_wlan_unsync_vif_activate(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	struct ieee80211_channel *chan = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);

	ndev_vif->mgmt_tx_gas_frame = true;
	slsi_wlan_unsync_vif_activate(sdev, dev, chan, 0);

	ndev_vif->mgmt_tx_gas_frame = false;
	ndev_vif->activated = true;
	ndev_vif->vif_type = FAPI_VIFTYPE_NAN;
	ndev_vif->ifnum = SLSI_NET_INDEX_NAN;
	slsi_wlan_unsync_vif_activate(sdev, dev, chan, 0);
}

#if !(defined(SCSC_SEP_VERSION) && SCSC_SEP_VERSION < 11)
static void test_mgt_slsi_get_valid_ssid_info_from_list(struct kunit *test)
{
	struct net_device     *dev = TEST_TO_DEV(test);
	struct netdev_vif     *ndev_vif = netdev_priv(dev);
	struct slsi_ssid_info *ssid_info = kunit_kzalloc(test, sizeof(*ssid_info), GFP_KERNEL);

	INIT_LIST_HEAD(&ndev_vif->sta.ssid_info);
	list_add(&ssid_info->list, &ndev_vif->sta.ssid_info);
	ssid_info->ssid.ssid_len = 10;
	memcpy(ssid_info->ssid.ssid, "test_ssid", ssid_info->ssid.ssid_len);
	ndev_vif->sta.ssid_len = ssid_info->ssid.ssid_len;
	memcpy(ndev_vif->sta.ssid, "test_ssid", ndev_vif->sta.ssid_len);
	ssid_info->akm_type = SLSI_BSS_SECURED_PSK;
	ndev_vif->sta.akm_type = ssid_info->akm_type;
	KUNIT_EXPECT_PTR_EQ(test, ssid_info, slsi_get_valid_ssid_info_from_list(dev));
}

static void test_mgt_slsi_get_valid_bssid_info_from_list(struct kunit *test)
{
	struct slsi_dev        *sdev = TEST_TO_SDEV(test);
	struct net_device      *dev = TEST_TO_DEV(test);
	struct netdev_vif      *ndev_vif = netdev_priv(dev);
	struct list_head       bssid_list;
	struct slsi_bssid_info *bssid_info = kunit_kzalloc(test, sizeof(*bssid_info), GFP_KERNEL);

	INIT_LIST_HEAD(&bssid_list);
	INIT_LIST_HEAD(&ndev_vif->acl_data_fw_list);
	list_add(&bssid_info->list, &bssid_list);
	bssid_info->connect_attempted = false;
	SLSI_ETHER_COPY(bssid_info->bssid, SLSI_DEFAULT_HW_MAC_ADDR);
	KUNIT_EXPECT_EQ(test, bssid_info, slsi_get_valid_bssid_info_from_list(sdev, dev, &bssid_list));
}

static void test_mgt_slsi_update_sta_sme(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct ieee80211_channel *chan = kunit_kzalloc(test, sizeof(struct ieee80211_channel), GFP_KERNEL);
	u8                       *ssid = "test_ssid";
	u8                       *bssid = SLSI_DEFAULT_HW_MAC_ADDR;
	u8                       ssid_len = strlen(ssid) + 1;

	slsi_update_sta_sme(sdev, dev, ssid, ssid_len, bssid, chan);
	kfree(ndev_vif->sta.sme.ssid);
	kfree(ndev_vif->sta.sme.bssid);
	kfree(ndev_vif->sta.sme.channel);
}

static void test_mgt_slsi_select_ap_for_connection(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	u8                       *bssid;
	struct ieee80211_channel *channel;
	bool                     retry = false;

	INIT_LIST_HEAD(&ndev_vif->sta.ssid_info);
	KUNIT_EXPECT_FALSE(test, slsi_select_ap_for_connection(sdev, dev, &bssid, &channel, retry));
}

static void test_mgt_slsi_set_reset_connect_attempted_flag(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct slsi_ssid_info    *ssid_info = kunit_kzalloc(test, sizeof(*ssid_info), GFP_KERNEL);
	struct slsi_bssid_info   *bssid_info = kunit_kzalloc(test, sizeof(*bssid_info), GFP_KERNEL);
	u8                       bssid;

	INIT_LIST_HEAD(&ndev_vif->sta.ssid_info);
	INIT_LIST_HEAD(&ssid_info->bssid_list);
	list_add(&ssid_info->list, &ndev_vif->sta.ssid_info);
	list_add(&bssid_info->list, &ssid_info->bssid_list);
	slsi_set_reset_connect_attempted_flag(sdev, dev, &bssid);

	slsi_set_reset_connect_attempted_flag(sdev, dev, NULL);
}
#endif

static void test_mgt_slsi_is_bssid_in_blacklist(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	u8                       *bssid = SLSI_DEFAULT_HW_MAC_ADDR;

	INIT_LIST_HEAD(&ndev_vif->acl_data_fw_list);
	KUNIT_EXPECT_EQ(test, false, slsi_is_bssid_in_blacklist(sdev, dev, bssid));
}

static void test_mgt_slsi_is_bssid_in_hal_blacklist(struct kunit *test)
{
	struct net_device        *dev = TEST_TO_DEV(test);
	u8                       *bssid = SLSI_DEFAULT_HW_MAC_ADDR;

	KUNIT_EXPECT_EQ(test, false, slsi_is_bssid_in_hal_blacklist(dev, bssid));
}

static void test_mgt_slsi_is_bssid_in_ioctl_blacklist(struct kunit *test)
{
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	u8                       *bssid = SLSI_DEFAULT_HW_MAC_ADDR;

	INIT_LIST_HEAD(&ndev_vif->acl_data_ioctl_list);
	KUNIT_EXPECT_EQ(test, false, slsi_is_bssid_in_ioctl_blacklist(dev, bssid));
}

static void test_mgt_slsi_add_ioctl_blacklist(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	u8                       *bssid = SLSI_DEFAULT_HW_MAC_ADDR;

	INIT_LIST_HEAD(&ndev_vif->acl_data_ioctl_list);
	KUNIT_EXPECT_EQ(test, false, slsi_add_ioctl_blacklist(sdev, dev, bssid));
}

static void test_mgt_slsi_remove_bssid_blacklist(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	u8                       *bssid = SLSI_DEFAULT_HW_MAC_ADDR;

	INIT_LIST_HEAD(&ndev_vif->acl_data_ioctl_list);
	INIT_LIST_HEAD(&ndev_vif->acl_data_fw_list);
	KUNIT_EXPECT_EQ(test, false, slsi_remove_bssid_blacklist(sdev, dev, bssid));
}

static void test_mgt_slsi_wlan_unsync_vif_deactivate(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct net_device        *dev = TEST_TO_DEV(test);

	slsi_wlan_unsync_vif_deactivate(sdev, dev, true);
}

static void test_mgt_slsi_scan_ind_timeout_handle(struct kunit *test)
{
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct delayed_work      *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);

	ndev_vif->scan_timeout_work = *work;
	slsi_scan_ind_timeout_handle(&ndev_vif->scan_timeout_work);
}

static void test_mgt_slsi_blacklist_del_work_handle(struct kunit *test)
{
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct delayed_work      *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);

	ndev_vif->blacklist_del_work = *work;
	INIT_LIST_HEAD(&ndev_vif->acl_data_fw_list);
	slsi_blacklist_del_work_handle(&ndev_vif->blacklist_del_work);
}

static void test_mgt_slsi_update_supported_channels_regd_flags(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);

	slsi_update_supported_channels_regd_flags(sdev);
}

static void test_mgt_slsi_find_chan_idx(struct kunit *test)
{
	u16 chan = SLSI_NUM_2P4GHZ_CHANNELS;
	u8  hw_mode = SLSI_ACS_MODE_IEEE80211B;
	int band = NL80211_BAND_6GHZ;

	KUNIT_EXPECT_LE(test, 0, slsi_find_chan_idx(chan, hw_mode, band));

	hw_mode = SLSI_ACS_MODE_IEEE80211A;
	KUNIT_EXPECT_LE(test, 0, slsi_find_chan_idx(chan, hw_mode, band));

	hw_mode = SLSI_ACS_MODE_IEEE80211AD;
	KUNIT_EXPECT_LE(test, 0, slsi_find_chan_idx(chan, hw_mode, band));

	band = NL80211_BAND_2GHZ;
	hw_mode = SLSI_ACS_MODE_IEEE80211AD;
	KUNIT_EXPECT_LE(test, 0, slsi_find_chan_idx(chan, hw_mode, band));

	band = NL80211_BAND_5GHZ;
	hw_mode = SLSI_ACS_MODE_IEEE80211AD;
	chan = 40;
	KUNIT_EXPECT_LE(test, 0, slsi_find_chan_idx(chan, hw_mode, band));

#ifdef CONFIG_SCSC_WLAN_SUPPORT_6G
	band = NL80211_BAND_6GHZ;
	chan = 2;
	KUNIT_EXPECT_LE(test, 0, slsi_find_chan_idx(chan, hw_mode, band));

	chan = 85;
	KUNIT_EXPECT_LE(test, 0, slsi_find_chan_idx(chan, hw_mode, band));
#endif
}

static void test_mgt_slsi_set_latency_mode(struct kunit *test)
{
	struct net_device        *dev = TEST_TO_DEV(test);
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	int                      latency_mode = 0;

	KUNIT_EXPECT_EQ(test, 0, slsi_set_latency_mode(dev, latency_mode, 0));

	latency_mode = 1;
	KUNIT_EXPECT_EQ(test, 0, slsi_set_latency_mode(dev, latency_mode, 0));
}

static void test_mgt_slsi_set_latency_crt_data(struct kunit *test)
{
	struct net_device        *dev = TEST_TO_DEV(test);
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	int                      latency_mode = 0;

	sdev->device_config.host_state = SLSI_HOSTSTATE_LOW_LATENCY_ACTIVE;
	KUNIT_EXPECT_EQ(test, 0, slsi_set_latency_crt_data(dev, latency_mode));

	sdev->device_config.host_state = 0;
	latency_mode = 2;
	KUNIT_EXPECT_EQ(test, 0, slsi_set_latency_crt_data(dev, latency_mode));

	latency_mode = 3;
	KUNIT_EXPECT_EQ(test, 0, slsi_set_latency_crt_data(dev, latency_mode));
}

#ifdef CONFIG_SCSC_WLAN_SAR_SUPPORTED
static void test_mgt_slsi_configure_tx_power_sar_scenario(struct kunit *test)
{
	struct net_device        *dev = TEST_TO_DEV(test);

	KUNIT_EXPECT_EQ(test, 0, slsi_configure_tx_power_sar_scenario(dev, 1));
}
#endif

static void test_mgt_slsi_subsystem_reset(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct work_struct       *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);

	sdev->recovery_work = *work;
	slsi_subsystem_reset(&sdev->recovery_work);
}

static void test_mgt_slsi_wakeup_time_work(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct work_struct       *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);

	sdev->wakeup_time_work = *work;
	slsi_wakeup_time_work(&sdev->wakeup_time_work);
}

static void test_mgt_slsi_trigger_service_failure(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct work_struct       *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);

	sdev->trigger_wlan_fail_work = *work;
	slsi_trigger_service_failure(&sdev->trigger_wlan_fail_work);
}

static void test_mgt_slsi_failure_reset(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct work_struct       *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);

	sdev->recovery_work_on_stop = *work;
	slsi_failure_reset(&sdev->recovery_work_on_stop);
}

static void test_mgt_slsi_chip_recovery(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct work_struct       *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);

	sdev->recovery_work_on_start = *work;
	slsi_chip_recovery(&sdev->recovery_work_on_start);
}

static void test_mgt_slsi_system_error_recovery(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct work_struct       *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);

	sdev->system_error_user_fail_work = *work;
	slsi_system_error_recovery(&sdev->system_error_user_fail_work);
}

static void test_mgt_slsi_collect_chipset_logs(struct kunit *test)
{
	struct slsi_dev          *sdev = TEST_TO_SDEV(test);
	struct work_struct       *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);

	sdev->system_error_user_fail_work = *work;
	slsi_collect_chipset_logs(&sdev->system_error_user_fail_work);
}

#ifdef CONFIG_SCSC_WLAN_DYNAMIC_ITO
static void test_mgt_slsi_set_ito(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	char              *command = "SET_ITO 100";

	KUNIT_EXPECT_EQ(test, 0, slsi_set_ito(dev, command, strlen(command) + 1));
}

static void test_mgt_slsi_enable_ito(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	char              *command = "ENABLE_ITO 1";

	KUNIT_EXPECT_EQ(test, 0, slsi_enable_ito(dev, command, strlen(command) + 1));
}
#endif

static void test_mgt_slsi_sort_array(struct kunit *test)
{
	u16 arr[2] = { 2, 1 };

	slsi_sort_array(arr, 2);
}

static void test_mgt_slsi_remove_duplicates(struct kunit *test)
{
	u16 arr[3] = { 2, 1, 2 };

	KUNIT_EXPECT_EQ(test, 3, slsi_remove_duplicates(arr, 3));
}

static void test_mgt_slsi_merge_lists(struct kunit *test)
{
	u16 ar1[2] = { 2, 1 };
	u16 ar2[2] = { 3, 2 };
	u16 result[4] = { 0, };

	KUNIT_EXPECT_EQ(test, 3, slsi_merge_lists(ar1, 2, ar2, 2, result));
}

static void test_mgt_slsi_rx_update_mlme_stats(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct sk_buff    *skb = fapi_alloc(mlme_scan_ind, MLME_SCAN_IND, 0, 128);
	struct ieee80211_mgmt *mgmt = fapi_get_mgmt(skb);

	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DEAUTH);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DISASSOC);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION);
#if defined(CONFIG_SLSI_WLAN_STA_FWD_BEACON) && (defined(SCSC_SEP_VERSION) && SCSC_SEP_VERSION >= 10)
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACO);
#endif

	slsi_rx_update_mlme_stats(sdev, skb);
	kfree_skb(skb);
}

static void test_mgt_slsi_rx_update_wake_stats(struct kunit *test)
{
	struct slsi_dev                         *sdev = TEST_TO_SDEV(test);
	struct slsi_wlan_driver_wake_reason_cnt *wake_reason_count;
	struct ethhdr                           *ehdr = kunit_kzalloc(test, sizeof(*ehdr), GFP_KERNEL);

	wake_reason_count = kunit_kzalloc(test, sizeof(*wake_reason_count), GFP_KERNEL);
	sdev->wake_reason_stats = *wake_reason_count;
	memset(ehdr->h_dest, 0xFF, ETH_ALEN);
	ehdr->h_proto = cpu_to_be16(ETH_P_IP);
	slsi_rx_update_wake_stats(sdev, ehdr, sizeof(struct ethhdr) + sizeof(struct iphdr), NULL);
}

static void test_mgt_slsi_get_scan_extra_ies(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	u8                *ies = NULL;
	int               total_len = 0;
	int               extra_len = 0;

	ies = slsi_get_scan_extra_ies(sdev, ies, total_len, &extra_len);
	KUNIT_EXPECT_NULL(test, ies);
}

static void test_mgt_slsi_add_probe_ies_request(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	KUNIT_EXPECT_EQ(test, 0, slsi_add_probe_ies_request(sdev, dev));
}

static void test_mgt_slsi_dump_eth_packet(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);
	struct sk_buff    *skb = fapi_alloc(mlme_scan_ind, MLME_SCAN_IND, 0, 128);

	KUNIT_EXPECT_EQ(test, -EINVAL, slsi_dump_eth_packet(sdev, skb));
	kfree_skb(skb);
}

static void test_mgt_slsi_send_twt_setup_event(struct kunit *test)
{
	struct net_device           *dev = TEST_TO_DEV(test);
	struct netdev_vif           *ndev_vif = netdev_priv(dev);
	struct slsi_dev             *sdev = ndev_vif->sdev;
	struct slsi_twt_setup_event setup_event;

	sdev->wiphy = kunit_kzalloc(test, sizeof(struct wiphy), GFP_KERNEL);
	sdev->wiphy->n_vendor_events = SLSI_NL80211_VENDOR_TWT_SETUP_EVENT + 1;
	KUNIT_EXPECT_EQ(test, 0, slsi_send_twt_setup_event(sdev, dev, setup_event));
}

static void test_mgt_slsi_send_twt_teardown(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	sdev->wiphy = kunit_kzalloc(test, sizeof(struct wiphy), GFP_KERNEL);
	sdev->wiphy->n_vendor_events = SLSI_NL80211_VENDOR_TWT_TEARDOWN_EVENT + 1;
	KUNIT_EXPECT_EQ(test, 0, slsi_send_twt_teardown(sdev, dev, 1, 0));
}

static void test_mgt_slsi_send_twt_notification(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	sdev->wiphy = kunit_kzalloc(test, sizeof(struct wiphy), GFP_KERNEL);
	sdev->wiphy->n_vendor_events = SLSI_NL80211_VENDOR_TWT_NOTIFICATION_EVENT + 1;
	KUNIT_EXPECT_EQ(test, 0, slsi_send_twt_notification(sdev, dev));
}

static void test_mgt_slsi_set_mib_obss_pd_enable(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	KUNIT_EXPECT_EQ(test, 0, slsi_set_mib_obss_pd_enable(sdev, dev, true, -10));
}

static void test_mgt_slsi_set_mib_obss_pd_enable_per_obss(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               rssi[1] = { -10 };
	u8                mac_addr[1][ETH_ALEN] = { {"00:ff:ff:ff:ff:ff"} };

	KUNIT_EXPECT_EQ(test, 0, slsi_set_mib_obss_pd_enable_per_obss(sdev, dev, 1, rssi, mac_addr));
}

static void test_mgt_slsi_set_mib_srp_non_srg_obss_pd_prohibited(struct kunit *test)
{
	struct slsi_dev   *sdev = TEST_TO_SDEV(test);

	KUNIT_EXPECT_EQ(test, SLSI_MIB_STATUS_FAILURE, slsi_set_mib_srp_non_srg_obss_pd_prohibited(sdev, false));
}

#ifdef CONFIG_SCSC_WLAN_SUPPORT_6G
static void test_mgt_slsi_set_mib_6g_safe_mode(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	slsi_set_mib_6g_safe_mode(sdev, 0);
}

static void test_mgt_slsi_set_mib_6g_rf_test_mode(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	slsi_set_mib_6g_rf_test_mode(sdev);
}
#endif

static void test_mgt_slsi_sysfs_pm(struct kunit *test)
{
	char *buf = kunit_kzalloc(test, 10, GFP_KERNEL);

	slsi_create_sysfs_pm();

	KUNIT_EXPECT_EQ(test, 1, sysfs_store_pm(NULL, NULL, "1", 1));
	KUNIT_EXPECT_EQ(test, 1, EnableRfTestMode);
	KUNIT_EXPECT_EQ(test, 0, sysfs_store_pm(NULL, NULL, "0", 0));
	KUNIT_EXPECT_EQ(test, 0, EnableRfTestMode);
	KUNIT_EXPECT_EQ(test, 3, sysfs_store_pm(NULL, NULL, "3", 3));
	KUNIT_EXPECT_EQ(test, 3, EnableRfTestMode);
	KUNIT_EXPECT_EQ(test, 0, sysfs_store_pm(NULL, NULL, "-", 3));
	KUNIT_EXPECT_EQ(test, 3, EnableRfTestMode);
	sysfs_show_pm(NULL, NULL, buf);
	KUNIT_EXPECT_STREQ(test, "3\n", buf);
	KUNIT_EXPECT_EQ(test, 1, slsi_is_rf_test_mode_enabled());

	slsi_destroy_sysfs_pm();
}

static void test_mgt_slsi_sysfs_ant(struct kunit *test)
{
	char *buf = kunit_kzalloc(test, 10, GFP_KERNEL);

	slsi_create_sysfs_ant();

	KUNIT_EXPECT_EQ(test, 1, sysfs_store_ant(NULL, NULL, "1", 1));
	KUNIT_EXPECT_EQ(test, 1, sysfs_antenna);
	KUNIT_EXPECT_EQ(test, 4, sysfs_store_ant(NULL, NULL, "4", 4));
	KUNIT_EXPECT_EQ(test, 1, sysfs_antenna);
	KUNIT_EXPECT_EQ(test, 0, sysfs_store_ant(NULL, NULL, "-", 4));
	KUNIT_EXPECT_EQ(test, 1, sysfs_antenna);
	sysfs_show_ant(NULL, NULL, buf);
	KUNIT_EXPECT_STREQ(test, "1\n", buf);

	slsi_destroy_sysfs_ant();
}

static void test_mgt_sysfs_store_max_log_size(struct kunit *test)
{
	char *buf = kunit_kzalloc(test, 10, GFP_KERNEL);

	KUNIT_EXPECT_EQ(test, 200, sysfs_store_max_log_size(NULL, NULL, "200", 200));
	KUNIT_EXPECT_EQ(test, 200, sysfs_max_log_size);
	KUNIT_EXPECT_EQ(test, 1025, sysfs_store_max_log_size(wifi_kobj_ref, NULL, "1025", 1025));
	KUNIT_EXPECT_EQ(test, 200, sysfs_max_log_size);
	KUNIT_EXPECT_EQ(test, 0, sysfs_store_max_log_size(wifi_kobj_ref, NULL, "==", 30));
	KUNIT_EXPECT_EQ(test, 200, sysfs_max_log_size);
	sysfs_show_max_log_size(NULL, NULL, buf);
	KUNIT_EXPECT_STREQ(test, "200\n", buf);
}

static void test_mgt_slsi_create_destroy_sysfs_max_log_size(struct kunit *test)
{
	slsi_create_sysfs_max_log_size();
	slsi_destroy_sysfs_max_log_size();
}

static void test_mgt_slsi_release_dp_resources(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	KUNIT_EXPECT_EQ(test, true, slsi_release_dp_resources(sdev, dev, ndev_vif));
	KUNIT_EXPECT_EQ(test, false, slsi_release_dp_resources(sdev, NULL, ndev_vif));
}

static void test_mgt_slsi_set_boolean_mib(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	KUNIT_EXPECT_EQ(test, 0, slsi_set_boolean_mib(sdev, NULL, SLSI_PSID_UNIFI24_G40_MHZ_CHANNELS, 1, 0));
}

#ifdef CONFIG_SCSC_WLAN_EHT
static void test_mgt_slsi_get_ap_mld_addr(struct kunit *test)
{
//int slsi_get_ap_mld_addr(struct slsi_dev *sdev, const struct element *ml_ie, u8 *mld_addr)
}
#endif

static void test_mgt_slsi_send_bw_changed_event(struct kunit *test)
{
	struct net_device *dev = TEST_TO_DEV(test);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	sdev->wiphy->n_vendor_events = SLSI_NL80211_VENDOR_BW_CHANGED_EVENT +1;

	KUNIT_EXPECT_EQ(test, 0, slsi_send_bw_changed_event(sdev, dev, 1));
}

static int mgt_test_init(struct kunit *test)
{
	test_dev_init(test);

	kunit_log(KERN_INFO, test, "%s: initialized.", __func__);
	return 0;
}

static void mgt_test_exit(struct kunit *test)
{
	kunit_log(KERN_INFO, test, "%s: completed.", __func__);
}

static struct kunit_case mgt_test_cases[] = {
	KUNIT_CASE(test_mgt_slsi_EnableRfTestMode_set),
	KUNIT_CASE(test_mgt_slsi_EnableRfTestMode_get),
	KUNIT_CASE(test_mgt_slsi_monitor_vif_num_set_cb),
	KUNIT_CASE(test_mgt_slsi_monitor_vif_num_get_cb),
	KUNIT_CASE(test_mgt_sysfs_show_macaddr),
	KUNIT_CASE(test_mgt_sysfs_store_macaddr),
	KUNIT_CASE(test_mgt_slsi_create_destroy_sysfs_macaddr),
	KUNIT_CASE(test_mgt_sysfs_show_version_info),
	KUNIT_CASE(test_mgt_slsi_create_destroy_sysfs_version_info),
	KUNIT_CASE(test_mgt_sysfs_show_debugdump),
	KUNIT_CASE(test_mgt_sysfs_store_debugdump),
	KUNIT_CASE(test_mgt_slsi_create_destroy_sysfs_debug_dump),
	KUNIT_CASE(test_mgt_slsi_purge_scan_results_locked),
	KUNIT_CASE(test_mgt_slsi_purge_scan_results),
	KUNIT_CASE(test_mgt_slsi_purge_blacklist),
	KUNIT_CASE(test_mgt_slsi_dequeue_cached_scan_result),
	KUNIT_CASE(test_mgt_slsi_copy_mac_valid),
	KUNIT_CASE(test_mgt_slsi_get_hw_mac_address),
	KUNIT_CASE(test_mgt_write_wifi_version_info_file),
	KUNIT_CASE(test_mgt_write_m_test_chip_version_file),
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	KUNIT_CASE(test_mgt_write_softap_info_file),
#endif
	KUNIT_CASE(test_mgt_slsi_start_monitor_mode),
	KUNIT_CASE(test_mgt_slsi_stop_monitor_mode),
#if IS_ENABLED(CONFIG_SCSC_LOG_COLLECTION)
	KUNIT_CASE(test_mgt_slsi_hcf_collect),
#endif
	KUNIT_CASE(test_mgt_slsi_clear_sys_error_buffer),
	KUNIT_CASE(test_mgt_slsi_wlan_recovery_init),
	KUNIT_CASE(test_mgt_slsi_start),
	KUNIT_CASE(test_mgt_slsi_dynamic_interface_create),
	KUNIT_CASE(test_mgt_slsi_stop_chip),
#ifdef CONFIG_SCSC_WIFI_NAN_ENABLE
	KUNIT_CASE(test_mgt_slsi_ndl_vif_cleanup),
#endif
	KUNIT_CASE(test_mgt_slsi_vif_cleanup),
	KUNIT_CASE(test_mgt_slsi_sched_scan_stopped),
	KUNIT_CASE(test_mgt_slsi_scan_cleanup),
	KUNIT_CASE(test_mgt_slsi_clear_low_latency_state),
	KUNIT_CASE(test_mgt_slsi_stop_net_dev_locked),
	KUNIT_CASE(test_mgt_slsi_stop_net_dev),
	KUNIT_CASE(test_mgt_slsi_stop),
#ifdef CONFIG_WLBT_LOCAL_MIB
	KUNIT_CASE(test_mgt_slsi_mib_slice),
#endif
	KUNIT_CASE(test_mgt_slsi_mib_get_platform),
	KUNIT_CASE(test_mgt_slsi_mib_open_file),
	KUNIT_CASE(test_mgt_slsi_mib_close_file),
#ifdef CONFIG_WLBT_LOCAL_MIB
	KUNIT_CASE(test_mgt_slsi_mib_download_file),
#endif
	KUNIT_CASE(test_mgt_slsi_process_supported_channels),
	KUNIT_CASE(test_mgt_slsi_get_ht_vht_capabilities),
#ifdef CONFIG_SCSC_WLAN_SUPPORT_6G
	KUNIT_CASE(test_mgt_slsi_get_6g_capabilities),
#endif
#if defined(CONFIG_SCSC_WLAN_TAS)
	KUNIT_CASE(test_mgt_slsi_tas_get_config),
#endif
#ifdef CONFIG_SCSC_WLAN_EHT
	KUNIT_CASE(test_mgt_slsi_get_mlo_capabilities),
#endif
	KUNIT_CASE(test_mgt_slsi_get_mib_entry_value),
	KUNIT_CASE(test_mgt_slsi_mib_initial_get),
	KUNIT_CASE(test_mgt_slsi_set_mib_roam),
	KUNIT_CASE(test_mgt_slsi_twt_update_ctrl_flags),
#ifdef CONFIG_SCSC_WLAN_SET_PREFERRED_ANTENNA
	KUNIT_CASE(test_mgt_slsi_set_mib_preferred_antenna),
#endif
	KUNIT_CASE(test_mgt_slsi_reset_throughput_stats),
	KUNIT_CASE(test_mgt_slsi_get_mib_roam),
#ifdef CONFIG_SCSC_WLAN_GSCAN_ENABLE
	KUNIT_CASE(test_mgt_slsi_mib_get_gscan_cap),
#endif
	KUNIT_CASE(test_mgt_slsi_mib_get_apf_cap),
	KUNIT_CASE(test_mgt_slsi_mib_get_rtt_cap),
	KUNIT_CASE(test_mgt_slsi_mib_get_sta_tdls_activated),
	KUNIT_CASE(test_mgt_slsi_mib_get_sta_tdls_max_peer),
#ifdef CONFIG_SCSC_WLAN_EHT
	KUNIT_CASE(test_mgt_slsi_get_link_peer_from_mac),
	KUNIT_CASE(test_mgt_slsi_sta_add_peer_link),
#endif
	KUNIT_CASE(test_mgt_slsi_peer_add),
	KUNIT_CASE(test_mgt_slsi_peer_reset_stats),
	KUNIT_CASE(test_mgt_slsi_dump_stats),
#if defined(CONFIG_SCSC_WLAN_ENHANCED_BIGDATA) && (defined(SCSC_SEP_VERSION) && SCSC_SEP_VERSION >= 10)
	KUNIT_CASE(test_mgt_slsi_substitute_null),
	KUNIT_CASE(test_mgt_slsi_fill_bigdata_record),
#endif
	KUNIT_CASE(test_mgt_slsi_send_hanged_vendor_event),
	KUNIT_CASE(test_mgt_slsi_send_power_measurement_vendor_event),
	KUNIT_CASE(test_mgt_slsi_set_ext_cap),
	KUNIT_CASE(test_mgt_slsi_search_ies_for_qos_indicators),
	KUNIT_CASE(test_mgt_slsi_peer_update_assoc_req),
	KUNIT_CASE(test_mgt_slsi_peer_update_assoc_rsp),
	KUNIT_CASE(test_mgt_slsi_peer_remove),
	KUNIT_CASE(test_mgt_slsi_vif_activated),
	KUNIT_CASE(test_mgt_slsi_vif_deactivated),
	KUNIT_CASE(test_mgt_slsi_sta_ieee80211_mode),
	KUNIT_CASE(test_mgt_slsi_populate_bss_record),
	KUNIT_CASE(test_mgt_slsi_fill_ap_sta_info_from_peer),
	KUNIT_CASE(test_mgt_slsi_fill_ap_sta_info_stats),
	KUNIT_CASE(test_mgt_slsi_fill_ap_sta_info),
#ifdef CONFIG_SCSC_WLAN_EHT
	KUNIT_CASE(test_mgt_slsi_ml_get_sta_bss),
#endif
	KUNIT_CASE(test_mgt_slsi_retry_connection),
	KUNIT_CASE(test_mgt_slsi_free_connection_params),
	KUNIT_CASE(test_mgt_slsi_handle_disconnect),
	KUNIT_CASE(test_mgt_slsi_ps_port_control),
	KUNIT_CASE(test_mgt_slsi_set_uint_mib),
	KUNIT_CASE(test_mgt_slsi_send_max_transmit_msdu_lifetime),
	KUNIT_CASE(test_mgt_slsi_read_max_transmit_msdu_lifetime),
#ifdef CONFIG_CFG80211_CRDA_SUPPORT
	KUNIT_CASE(test_mgt_slsi_update_custom_regulatory_orig_flags),
#endif
	KUNIT_CASE(test_mgt_slsi_band_cfg_update),
	KUNIT_CASE(test_mgt_slsi_band_update),
	KUNIT_CASE(test_mgt_slsi_disconnect_on_band_update),
	KUNIT_CASE(test_mgt_slsi_send_gratuitous_arp),
	KUNIT_CASE(test_mgt_slsi_create_packet_filter_element),
	KUNIT_CASE(test_mgt_slsi_set_common_packet_filters),
	KUNIT_CASE(test_mgt_slsi_set_arp_packet_filter),
#ifdef CONFIG_SCSC_WLAN_ENHANCED_PKT_FILTER
	KUNIT_CASE(test_mgt_slsi_set_enhanced_pkt_filter),
	KUNIT_CASE(test_mgt_slsi_set_opt_out_unicast_packet_filter),
	KUNIT_CASE(test_mgt_slsi_set_opt_in_tcp4_packet_filter),
	KUNIT_CASE(test_mgt_slsi_set_opt_in_tcp6_packet_filter),
#endif
	KUNIT_CASE(test_mgt_slsi_set_multicast_packet_filters),
	KUNIT_CASE(test_mgt_slsi_clear_packet_filters),
	KUNIT_CASE(test_mgt_slsi_update_packet_filters),
	KUNIT_CASE(test_mgt_slsi_set_packet_filters),
	KUNIT_CASE(test_mgt_slsi_ip_address_changed),
	KUNIT_CASE(test_mgt_slsi_auto_chan_select_scan),
	KUNIT_CASE(test_mgt_slsi_set_boost),
	KUNIT_CASE(test_mgt_slsi_p2p_roc_duration_expiry_work),
	KUNIT_CASE(test_mgt_slsi_p2p_unsync_vif_delete_work),
	KUNIT_CASE(test_mgt_slsi_p2p_unset_channel_expiry_work),
	KUNIT_CASE(test_mgt_slsi_p2p_init),
	KUNIT_CASE(test_mgt_slsi_p2p_deinit),
	KUNIT_CASE(test_mgt_slsi_p2p_vif_activate),
	KUNIT_CASE(test_mgt_slsi_p2p_vif_deactivate),
	KUNIT_CASE(test_mgt_slsi_p2p_group_start_remove_unsync_vif),
	KUNIT_CASE(test_mgt_slsi_p2p_dev_probe_rsp_ie),
	KUNIT_CASE(test_mgt_slsi_p2p_dev_null_ies),
	KUNIT_CASE(test_mgt_slsi_get_public_action_subtype),
	KUNIT_CASE(test_mgt_slsi_p2p_get_action_frame_status),
	KUNIT_CASE(test_mgt_slsi_get_exp_peer_frame_subtype),
#if !(defined(SCSC_SEP_VERSION) && SCSC_SEP_VERSION < 11) || defined(CONFIG_SCSC_WLAN_SUPPORT_6G)
	KUNIT_CASE(test_mgt_slsi_bss_connect_type_get),
#endif
	KUNIT_CASE(test_mgt_slsi_wlan_dump_public_action_subtype),
	KUNIT_CASE(test_mgt_slsi_abort_sta_scan),
	KUNIT_CASE(test_mgt_slsi_is_dhcp_packet),
#ifdef CONFIG_SCSC_WLAN_PRIORITISE_IMP_FRAMES
	KUNIT_CASE(test_mgt_slsi_is_tcp_sync_packet),
	KUNIT_CASE(test_mgt_slsi_is_dns_packet),
	KUNIT_CASE(test_mgt_slsi_is_mdns_packet),
#endif
	KUNIT_CASE(test_mgt_slsi_ap_prepare_add_info_ies),
	KUNIT_CASE(test_mgt_slsi_get_channel_jiffies_index),
	KUNIT_CASE(test_mgt_slsi_roam_channel_cache_add_channel),
	KUNIT_CASE(test_mgt_slsi_roam_channel_cache_add_entry),
	KUNIT_CASE(test_mgt_slsi_roam_channel_cache_add),
	KUNIT_CASE(test_mgt_slsi_roam_channel_cache_prune),
	KUNIT_CASE(test_mgt_slsi_roam_channel_cache_get_channels_int),
	KUNIT_CASE(test_mgt_slsi_roam_channel_cache_get),
	KUNIT_CASE(test_mgt_slsi_roam_channel_cache_get_channels),
	KUNIT_CASE(test_mgt_slsi_roaming_scan_configure_channels),
	KUNIT_CASE(test_mgt_slsi_send_rcl_event),
	KUNIT_CASE(test_mgt_slsi_send_acs_event),
#ifdef CONFIG_SCSC_WLAN_WES_NCHO
	KUNIT_CASE(test_mgt_slsi_is_wes_action_frame),
#endif
	KUNIT_CASE(test_mgt_slsi_remap_reg_rule_flags),
	KUNIT_CASE(test_mgt_slsi_reset_channel_flags),
	KUNIT_CASE(test_mgt_slsi_read_regulatory_rules),
	KUNIT_CASE(test_mgt_slsi_set_mib_rssi_boost),
#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING_CSA_LEGACY
	KUNIT_CASE(test_mgt_slsi_if_valid_wifi_sharing_channel),
	KUNIT_CASE(test_mgt_slsi_check_if_non_indoor_non_dfs_channel),
	KUNIT_CASE(test_mgt_slsi_get_mhs_ws_chan_vsdb),
	KUNIT_CASE(test_mgt_slsi_get_mhs_ws_chan_rsdb),
	KUNIT_CASE(test_mgt_slsi_check_if_channel_restricted_already),
#endif
#ifdef CONFIG_SCSC_WLAN_ENABLE_MAC_RANDOMISATION
	KUNIT_CASE(test_mgt_slsi_set_mac_randomisation_mask),
#endif
	KUNIT_CASE(test_mgt__set_country_update_regd),
	KUNIT_CASE(test_mgt_slsi_set_country_update_regd),
	KUNIT_CASE(test_mgt_slsi_read_disconnect_ind_timeout),
	KUNIT_CASE(test_mgt_slsi_get_beacon_cu),
	KUNIT_CASE(test_mgt_slsi_get_ps_disabled_duration),
	KUNIT_CASE(test_mgt_slsi_get_ps_entry_counter),
	KUNIT_CASE(test_mgt_slsi_clear_offchannel_data),
	KUNIT_CASE(test_mgt_slsi_hs2_unsync_vif_delete_work),
	KUNIT_CASE(test_mgt_slsi_wlan_unsync_vif_activate),
#if !(defined(SCSC_SEP_VERSION) && SCSC_SEP_VERSION < 11)
	KUNIT_CASE(test_mgt_slsi_get_valid_ssid_info_from_list),
	KUNIT_CASE(test_mgt_slsi_get_valid_bssid_info_from_list),
	KUNIT_CASE(test_mgt_slsi_update_sta_sme),
	KUNIT_CASE(test_mgt_slsi_select_ap_for_connection),
	KUNIT_CASE(test_mgt_slsi_set_reset_connect_attempted_flag),
#endif
	KUNIT_CASE(test_mgt_slsi_is_bssid_in_blacklist),
	KUNIT_CASE(test_mgt_slsi_is_bssid_in_hal_blacklist),
	KUNIT_CASE(test_mgt_slsi_is_bssid_in_ioctl_blacklist),
	KUNIT_CASE(test_mgt_slsi_add_ioctl_blacklist),
	KUNIT_CASE(test_mgt_slsi_remove_bssid_blacklist),
	KUNIT_CASE(test_mgt_slsi_wlan_unsync_vif_deactivate),
	KUNIT_CASE(test_mgt_slsi_scan_ind_timeout_handle),
	KUNIT_CASE(test_mgt_slsi_blacklist_del_work_handle),
	KUNIT_CASE(test_mgt_slsi_update_supported_channels_regd_flags),
	KUNIT_CASE(test_mgt_slsi_find_chan_idx),
	KUNIT_CASE(test_mgt_slsi_set_latency_mode),
	KUNIT_CASE(test_mgt_slsi_set_latency_crt_data),
#ifdef CONFIG_SCSC_WLAN_SAR_SUPPORTED
	KUNIT_CASE(test_mgt_slsi_configure_tx_power_sar_scenario),
#endif
	KUNIT_CASE(test_mgt_slsi_subsystem_reset),
	KUNIT_CASE(test_mgt_slsi_wakeup_time_work),
	KUNIT_CASE(test_mgt_slsi_trigger_service_failure),
	KUNIT_CASE(test_mgt_slsi_failure_reset),
	KUNIT_CASE(test_mgt_slsi_chip_recovery),
	KUNIT_CASE(test_mgt_slsi_system_error_recovery),
	KUNIT_CASE(test_mgt_slsi_collect_chipset_logs),
#ifdef CONFIG_SCSC_WLAN_DYNAMIC_ITO
	KUNIT_CASE(test_mgt_slsi_set_ito),
	KUNIT_CASE(test_mgt_slsi_enable_ito),
#endif
	KUNIT_CASE(test_mgt_slsi_sort_array),
	KUNIT_CASE(test_mgt_slsi_remove_duplicates),
	KUNIT_CASE(test_mgt_slsi_merge_lists),
	KUNIT_CASE(test_mgt_slsi_rx_update_mlme_stats),
	KUNIT_CASE(test_mgt_slsi_rx_update_wake_stats),
	KUNIT_CASE(test_mgt_slsi_get_scan_extra_ies),
	KUNIT_CASE(test_mgt_slsi_add_probe_ies_request),
	KUNIT_CASE(test_mgt_slsi_dump_eth_packet),
	KUNIT_CASE(test_mgt_slsi_send_twt_setup_event),
	KUNIT_CASE(test_mgt_slsi_send_twt_teardown),
	KUNIT_CASE(test_mgt_slsi_send_twt_notification),
	KUNIT_CASE(test_mgt_slsi_set_mib_obss_pd_enable),
	KUNIT_CASE(test_mgt_slsi_set_mib_obss_pd_enable_per_obss),
	KUNIT_CASE(test_mgt_slsi_set_mib_srp_non_srg_obss_pd_prohibited),
#ifdef CONFIG_SCSC_WLAN_SUPPORT_6G
	KUNIT_CASE(test_mgt_slsi_set_mib_6g_safe_mode),
	KUNIT_CASE(test_mgt_slsi_set_mib_6g_rf_test_mode),
#endif
	KUNIT_CASE(test_mgt_slsi_sysfs_pm),
	KUNIT_CASE(test_mgt_slsi_sysfs_ant),
	KUNIT_CASE(test_mgt_sysfs_store_max_log_size),
	KUNIT_CASE(test_mgt_slsi_create_destroy_sysfs_max_log_size),
	KUNIT_CASE(test_mgt_slsi_release_dp_resources),
	KUNIT_CASE(test_mgt_slsi_set_boolean_mib),
#ifdef CONFIG_SCSC_WLAN_EHT
	KUNIT_CASE(test_mgt_slsi_get_ap_mld_addr),
#endif
	KUNIT_CASE(test_mgt_slsi_send_bw_changed_event),
	{}
};

static struct kunit_suite mgt_test_suite[] = {
	{
		.name = "kunit-mgt-test",
		.test_cases = mgt_test_cases,
		.init = mgt_test_init,
		.exit = mgt_test_exit,
	}
};

kunit_test_suites(mgt_test_suite);
