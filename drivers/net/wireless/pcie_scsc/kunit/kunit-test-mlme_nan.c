#include <kunit/test.h>
#include <linux/cdev.h>

#include "kunit-common.h"
#include "kunit-mock-kernel.h"
#include "kunit-mock-dev.h"
#include "kunit-mock-netif.h"
#include "kunit-mock-mlme.h"
#include "kunit-mock-nl80211_vendor_nan.h"
#include "../mlme_nan.c"

#define SLSI_DEFAULT_BSSID		"\x01\x02\xDF\x1E\xB2\x39"
#define SLSI_DEFAULT_HW_MAC_ADDR	"\x00\x00\x0F\x11\x22\x33"

static inline void test_nan_dev_init(struct kunit *test)
{
	struct wiphy      *wiphy = NULL;
	struct slsi_dev   *sdev = NULL;
	struct net_device *dev = NULL;
	struct netdev_vif *ndev_vif = NULL;

	/* wiphy_new */
	wiphy = (struct wiphy *)kunit_kzalloc(test, sizeof(struct wiphy) + sizeof(struct slsi_dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, wiphy);

	sdev = SDEV_FROM_WIPHY(wiphy);
	sdev->wiphy = wiphy;

	dev = test_netdev_init(test, sdev, SLSI_NET_INDEX_NAN);
	dev->dev_addr = SLSI_DEFAULT_HW_MAC_ADDR;
	memcpy(dev->name, "TEST", 5);

	ndev_vif = netdev_priv(dev);
	ndev_vif->sdev = sdev;
	test->priv = dev;

	called_cfg80211_vendor_event = 0;
}

static void test_mlme_nan_slsi_mlme_nan_enable(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_hal_nan_enable_req *hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);

	hal_req->config_support_5g = 1;
	hal_req->support_5g_val = 1;
	hal_req->config_sid_beacon = 1;
	hal_req->sid_beacon_val = 0x01;
	hal_req->config_subscribe_sid_beacon = 1;
	hal_req->subscribe_sid_beacon_val = 0x01;
	hal_req->enable_instant_mode = 1;
	KUNIT_EXPECT_EQ(test, 0, slsi_mlme_nan_enable(sdev, dev, hal_req));
}

static void test_mlme_nan_slsi_mlme_nan_publish(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_hal_nan_publish_req *hal_req;
	int publish_id = 1;
	
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_publish(sdev, dev, hal_req, publish_id));

	hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);
	hal_req->service_name_len = 1;
	hal_req->service_specific_info_len = 1;
	hal_req->sdea_service_specific_info_len = 1;
	hal_req->rx_match_filter_len = 1;
	hal_req->tx_match_filter_len = 1;
	hal_req->sdea_params.config_nan_data_path = 1;
	hal_req->sdea_params.ranging_state = 1;
	hal_req->sec_info.key_info.key_type = 1;
	hal_req->recv_indication_cfg = 1;
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_publish(sdev, dev, hal_req, publish_id));

	hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);
	hal_req->service_name_len = 1;
	hal_req->service_specific_info_len = 1;
	hal_req->sdea_service_specific_info_len = 1;
	hal_req->rx_match_filter_len = 1;
	hal_req->tx_match_filter_len = 1;
	hal_req->sdea_params.config_nan_data_path = 1;
	hal_req->sdea_params.ranging_state = 1;
	hal_req->sec_info.key_info.key_type = 1;
	hal_req->recv_indication_cfg = 2;
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_publish(sdev, dev, hal_req, publish_id));

	hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);
	hal_req->service_name_len = 1;
	hal_req->service_specific_info_len = 1;
	hal_req->sdea_service_specific_info_len = 1;
	hal_req->rx_match_filter_len = 1;
	hal_req->tx_match_filter_len = 1;
	hal_req->sdea_params.config_nan_data_path = 1;
	hal_req->sdea_params.ranging_state = 1;
	hal_req->sec_info.key_info.key_type = 1;
	hal_req->recv_indication_cfg = 4;
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_publish(sdev, dev, hal_req, publish_id));

	hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);
	hal_req->service_name_len = 1;
	hal_req->service_specific_info_len = 1;
	hal_req->sdea_service_specific_info_len = 1;
	hal_req->rx_match_filter_len = 1;
	hal_req->tx_match_filter_len = 1;
	hal_req->sdea_params.config_nan_data_path = 1;
	hal_req->sdea_params.ranging_state = 1;
	hal_req->sec_info.key_info.key_type = 2;
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_publish(sdev, dev, hal_req, publish_id));
}

static void test_mlme_nan_slsi_mlme_nan_subscribe(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_hal_nan_subscribe_req *hal_req;
	int subscribe_id = 1;

	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_subscribe(sdev, dev, hal_req, subscribe_id));

	hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);
	hal_req->service_name_len = 1;
	hal_req->service_specific_info_len = 1;
	hal_req->sdea_service_specific_info_len = 1;
	hal_req->rx_match_filter_len = 1;
	hal_req->tx_match_filter_len = 1;
	hal_req->sdea_params.config_nan_data_path = 1;
	hal_req->sdea_params.ranging_state = 1;
	hal_req->recv_indication_cfg = 1;
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_subscribe(sdev, dev, hal_req, subscribe_id));

	hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);
	hal_req->service_name_len = 1;
	hal_req->service_specific_info_len = 1;
	hal_req->sdea_service_specific_info_len = 1;
	hal_req->rx_match_filter_len = 1;
	hal_req->tx_match_filter_len = 1;
	hal_req->sdea_params.config_nan_data_path = 1;
	hal_req->sdea_params.ranging_state = 1;
	hal_req->recv_indication_cfg = 2;
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_subscribe(sdev, dev, hal_req, subscribe_id));

	hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);
	hal_req->service_name_len = 1;
	hal_req->service_specific_info_len = 1;
	hal_req->sdea_service_specific_info_len = 1;
	hal_req->rx_match_filter_len = 1;
	hal_req->tx_match_filter_len = 1;
	hal_req->sdea_params.config_nan_data_path = 1;
	hal_req->sdea_params.ranging_state = 1;
	hal_req->recv_indication_cfg = 4;
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_subscribe(sdev, dev, hal_req, subscribe_id));
}

static void test_mlme_nan_slsi_mlme_nan_tx_followup(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_hal_nan_transmit_followup_req *hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);

	hal_req->requestor_instance_id = 1;
	hal_req->transaction_id = 1;
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_tx_followup(sdev, dev, hal_req));
}

static void test_mlme_nan_slsi_mlme_nan_set_config(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_hal_nan_config_req *hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);

	ndev_vif->nan.disable_cluster_merge = 0;
	hal_req->enable_instant_mode = 1;
	hal_req->master_pref = 0;
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_set_config(sdev, dev, hal_req));
}

static void test_mlme_nan_slsi_mlme_ndp_request(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_hal_nan_data_path_initiator_req *hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);
	int ndp_instance_id = 1;
	int ndl_vif_id = 1;
	int pos = 0;

	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	dev->dev_addr = SLSI_DEFAULT_HW_MAC_ADDR;
	hal_req->app_info.ndp_app_info_len = 10;
	hal_req->app_info.ndp_app_info[pos] = 0x01;
	hal_req->service_name_len = 1;
	memcpy(hal_req->ndp_iface, "TEST", 5);
	KUNIT_EXPECT_EQ(test, 0, slsi_mlme_ndp_request(sdev, dev, hal_req, ndp_instance_id, ndl_vif_id));

	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	dev->dev_addr = SLSI_DEFAULT_HW_MAC_ADDR;
	hal_req->app_info.ndp_app_info_len = 10;
	hal_req->app_info.ndp_app_info[pos] = 0x01;
	hal_req->service_name_len = 1;
	memcpy(hal_req->ndp_iface, "TEST", 5);
	KUNIT_EXPECT_EQ(test, 0, slsi_mlme_ndp_request(sdev, dev, hal_req, ndp_instance_id, ndl_vif_id));
}

static void test_mlme_nan_slsi_mlme_ndp_response(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_hal_nan_data_path_indication_response *hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);
	int ndp_instance_id = 1;
	int ndl_vif_id = 1;

	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	dev->dev_addr = SLSI_DEFAULT_HW_MAC_ADDR;
	hal_req->app_info.ndp_app_info_len = 1;
	hal_req->service_name_len = 1;
	hal_req->ndp_instance_id = 1;
	hal_req->rsp_code = NAN_DP_REQUEST_ACCEPT;
	memcpy(hal_req->ndp_iface, "TEST", 5);
	KUNIT_EXPECT_EQ(test, 0, slsi_mlme_ndp_response(sdev, dev, hal_req, ndp_instance_id));
}

static void test_mlme_nan_slsi_mlme_ndp_terminate(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	int ndp_instance_id = 1;
	int transaction_id = 1;

	ndev_vif->nan.ndp_state[ndp_instance_id - 1] = 0;
	KUNIT_EXPECT_EQ(test, 0, slsi_mlme_ndp_terminate(sdev, dev, ndp_instance_id, transaction_id));

	ndev_vif->nan.ndp_state[ndp_instance_id - 1] = 1;
	KUNIT_EXPECT_EQ(test, 0, slsi_mlme_ndp_terminate(sdev, dev, ndp_instance_id, transaction_id));
}

static void test_mlme_nan_slsi_mlme_nan_range_req(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_rtt_config *nl_rtt_params = kunit_kzalloc(test, sizeof(*nl_rtt_params), GFP_KERNEL);
	int count = 1;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	memcpy(nl_rtt_params[count - 1].peer_addr, SLSI_DEFAULT_HW_MAC_ADDR, ETH_ALEN);	
	nl_rtt_params[count - 1].num_frames_per_burst = 1;
	nl_rtt_params[count - 1].burst_duration = 1;
	nl_rtt_params[count - 1].num_retries_per_ftmr = 1;
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_nan_range_req(sdev, dev, count, nl_rtt_params));
}

static void test_mlme_nan_slsi_mlme_nan_range_cancel_req(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_rtt_config *nl_rtt_params = kunit_kzalloc(test, sizeof(*nl_rtt_params), GFP_KERNEL);
	int count = 1;
	int transaction_id = 1;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	KUNIT_EXPECT_EQ(test, 0, slsi_mlme_nan_range_cancel_req(sdev, dev));
}

static void test_mlme_nan_slsi_mlme_vendor_nan_set_command(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_hal_nan_vendor_cmd_data *hal_req = kunit_kzalloc(test, sizeof(*hal_req), GFP_KERNEL);
	struct slsi_hal_nan_vendor_cmd_resp_data *hal_cfm = kunit_kzalloc(test, sizeof(*hal_cfm), GFP_KERNEL);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	KUNIT_EXPECT_EQ(test, -22, slsi_mlme_vendor_nan_set_command(sdev, dev, hal_req, &hal_cfm));
}

static int mlme_nan_test_init(struct kunit *test)
{
	test_nan_dev_init(test);

	kunit_log(KERN_INFO, test, "%s: initialized.", __func__);
	return 0;
}

static void mlme_nan_test_exit(struct kunit *test)
{
	kunit_log(KERN_INFO, test, "%s: completed.", __func__);
}

static struct kunit_case mlme_nan_test_cases[] = {
	KUNIT_CASE(test_mlme_nan_slsi_mlme_nan_enable),
	KUNIT_CASE(test_mlme_nan_slsi_mlme_nan_publish),
	KUNIT_CASE(test_mlme_nan_slsi_mlme_nan_subscribe),
	KUNIT_CASE(test_mlme_nan_slsi_mlme_nan_tx_followup),
	KUNIT_CASE(test_mlme_nan_slsi_mlme_nan_set_config),
	KUNIT_CASE(test_mlme_nan_slsi_mlme_ndp_request),
	KUNIT_CASE(test_mlme_nan_slsi_mlme_ndp_response),
	KUNIT_CASE(test_mlme_nan_slsi_mlme_ndp_terminate),
	KUNIT_CASE(test_mlme_nan_slsi_mlme_nan_range_req),
	KUNIT_CASE(test_mlme_nan_slsi_mlme_nan_range_cancel_req),
	KUNIT_CASE(test_mlme_nan_slsi_mlme_vendor_nan_set_command),
	{}
};

static struct kunit_suite mlme_nan_test_suite[] = {
	{
		.name = "kunit-mlme_nan-test",
		.test_cases = mlme_nan_test_cases,
		.init = mlme_nan_test_init,
		.exit = mlme_nan_test_exit,
	}
};

kunit_test_suites(mlme_nan_test_suite);
