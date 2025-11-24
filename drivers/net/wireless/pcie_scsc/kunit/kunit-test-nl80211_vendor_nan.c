#include <kunit/test.h>
#include <linux/cdev.h>

#include "kunit-common.h"
#include "kunit-mock-kernel.h"
#include "kunit-mock-dev.h"
#include "kunit-mock-netif.h"
#include "kunit-mock-mlme_nan.h"
#include "kunit-mock-mlme.h"
#include "kunit-mock-mgt.h"
#include "../nl80211_vendor_nan.c"

#define SLSI_DEFAULT_BSSID		"\x01\x02\xDF\x1E\xB2\x39"
#define SLSI_DEFAULT_HW_MAC_ADDR	"\x00\x00\x0F\x11\x22\x33"

int val = 1;

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

	ndev_vif = netdev_priv(dev);
	ndev_vif->sdev = sdev;
	test->priv = dev;

	called_cfg80211_vendor_event = 0;
}

static void test_nl80211_vendor_nan_slsi_nan_dump_vif_data(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_vif_nan nan = ndev_vif->nan;


	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	nan.disc_info = kunit_kzalloc(test, sizeof(struct slsi_nan_discovery_info), GFP_KERNEL);
	slsi_nan_dump_vif_data(sdev, ndev_vif);
}

static void test_nl80211_vendor_nan_slsi_nan_enable(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct slsi_hal_nan_vendor_prev_cmd_data_info *prev_cmd_data_info;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;
	
	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	prev_cmd_data_info = kunit_kzalloc(test, sizeof(struct slsi_hal_nan_vendor_prev_cmd_data_info), GFP_KERNEL);
	list_add(&prev_cmd_data_info->list, &sdev->slsi_hal_nan_vendor_prev_cmd_data);
	/* Queue enable params */
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_MASTER_PREF, 1);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_CLUSTER_LOW, 1);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_CLUSTER_HIGH, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUPPORT_5G_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SID_BEACON_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RSSI_CLOSE_2G4_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RSSI_MIDDLE_2G4_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RSSI_PROXIMITY_2G4_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_HOP_COUNT_LIMIT_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUPPORT_2G4_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_BEACONS_2G4_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDF_2G4_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_BEACON_5G_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDF_5G_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RSSI_CLOSE_5G_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RSSI_MIDDLE_5G_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RSSI_CLOSE_PROXIMITY_5G_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RSSI_WINDOW_SIZE_VAL, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_OUI_VAL, 1);
	nla_put(nl_skb, NAN_REQ_ATTR_MAC_ADDR_VAL, ETH_ALEN, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CLUSTER_VAL, 1);
	nla_put(nl_skb, NAN_REQ_ATTR_SOCIAL_CH_SCAN_DWELL_TIME, 10, "0");
	nla_put(nl_skb, NAN_REQ_ATTR_SOCIAL_CH_SCAN_PERIOD, 10, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RANDOM_FACTOR_FORCE_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_HOP_COUNT_FORCE_VAL, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_CHANNEL_2G4_MHZ_VAL, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_CHANNEL_5G_MHZ_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_SID_BEACON_VAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_DW_2G4_INTERVAL, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_DW_5G_INTERVAL, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_DISC_MAC_ADDR_RANDOM_INTERVAL, 1);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_DISCOVERY_BEACON_INT, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_NSS, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_ENABLE_RANGING, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_DW_EARLY_TERMINATION, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_ENABLE_INSTANT_MODE, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_INSTANT_MODE_CHANNEL, 1);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_enable(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_DISCONNECTING;
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_enable(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_MASTER_PREF, 1);
	nlmsg_end(nl_skb, nl_hdr);
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_DISCONNECTING;
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_enable(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_MASTER_PREF, 1);
	nlmsg_end(nl_skb, nl_hdr);
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_CONNECTED;
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_enable(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_disable(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;;
	struct net_device 	*data_dev;
	struct netdev_vif 	*data_ndev_vif;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;

	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	ndev_vif->activated = false;
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_disable(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	ndev_vif->activated = true;
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START] = kunit_kzalloc(test, sizeof(struct net_device), GFP_KERNEL);
	data_ndev_vif = netdev_priv(sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]);
	data_ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	data_ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]->valid = true;
	struct slsi_peer *peer = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_disable(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_publish(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_ID, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_TTL, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_PERIOD, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_PUBLISH_TYPE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_PUBLISH_TX_TYPE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_PUBLISH_COUNT, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_SERVICE_NAME_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_PUBLISH_SERVICE_NAME, NAN_REQ_ATTR_PUBLISH_SERVICE_NAME_LEN, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_PUBLISH_MATCH_ALGO, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_SERVICE_INFO_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_PUBLISH_SERVICE_INFO, NAN_REQ_ATTR_PUBLISH_SERVICE_INFO_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_RX_MATCH_FILTER_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_PUBLISH_RX_MATCH_FILTER, NAN_REQ_ATTR_PUBLISH_RX_MATCH_FILTER_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_TX_MATCH_FILTER_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_PUBLISH_TX_MATCH_FILTER, NAN_REQ_ATTR_PUBLISH_TX_MATCH_FILTER_LEN, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_PUBLISH_RSSI_THRESHOLD_FLAG, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_PUBLISH_CONN_MAP, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_PUBLISH_RECV_IND_CFG, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_SDEA_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_PUBLISH_SDEA, NAN_REQ_ATTR_PUBLISH_SDEA_LEN, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RANGING_AUTO_RESPONSE, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 0);
	// slsi_nan_get_sdea_params_nl
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_NDP_TYPE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_SECURITY_CFG, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_RANGING_STATE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_RANGE_REPORT, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_QOS_CFG, 0);
	// slsi_nan_get_ranging_cfg_nl
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGING_CFG_INTERVAL, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGING_CFG_INDICATION, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGING_CFG_INGRESS_MM, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGING_CFG_EGRESS_MM, 0);
	// slsi_nan_get_security_info_nl
	nla_put_u32(nl_skb, NAN_REQ_ATTR_CIPHER_TYPE, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_SECURITY_KEY_TYPE, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_SECURITY_PMK_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SECURITY_PMK, NAN_REQ_ATTR_SECURITY_PMK_LEN, "0");
	nla_put_u32(nl_skb, NAN_REQ_ATTR_SECURITY_PASSPHRASE_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SECURITY_PASSPHRASE, NAN_REQ_ATTR_SECURITY_PASSPHRASE_LEN, "0");
	nla_put_u32(nl_skb, NAN_REQ_ATTR_SCID_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SCID, NAN_REQ_ATTR_SCID_LEN, "0");
	// slsi_nan_get_range_resp_cfg_nl
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGE_RESPONSE_CFG_PUBLISH_ID, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_RANGE_RESPONSE_CFG_REQUESTOR_ID, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_RANGE_RESPONSE_CFG_PEER_ADDR, ETH_ALEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGE_RESPONSE_CFG_RANGING_RESPONSE, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_publish(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_DISCONNECTING;
	KUNIT_EXPECT_EQ(test, -22, slsi_nan_publish(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_ID, 0);
	nlmsg_end(nl_skb, nl_hdr);
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_DISCONNECTING;
	KUNIT_EXPECT_EQ(test, -10, slsi_nan_publish(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_ID, 0);
	nlmsg_end(nl_skb, nl_hdr);
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_CONNECTED;
	KUNIT_EXPECT_EQ(test, -10, slsi_nan_publish(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_publish_cancel(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct net_device 	*data_dev;
	struct netdev_vif 	*data_ndev_vif;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);	
	ndev_vif->activated = false;
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 1, slsi_nan_publish_cancel(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);	
	ndev_vif->activated = true;
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_ID, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 0);
	nlmsg_end(nl_skb, nl_hdr);
	set_bit(0, ndev_vif->nan.service_id_map);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START] = kunit_kzalloc(test, sizeof(struct net_device), GFP_KERNEL);
	data_ndev_vif = netdev_priv(sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]);
	data_ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	data_ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]->valid = true;
	struct slsi_peer *peer = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 1, slsi_nan_publish_cancel(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_subscribe(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_ID, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_TTL, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_PERIOD, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_TYPE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_RESP_FILTER_TYPE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_RESP_INCLUDE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_USE_RESP_FILTER, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_SSI_REQUIRED, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_MATCH_INDICATOR, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_COUNT, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_SERVICE_NAME_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_SERVICE_NAME, NAN_REQ_ATTR_SUBSCRIBE_SERVICE_NAME_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_SERVICE_INFO_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_SERVICE_INFO, NAN_REQ_ATTR_SUBSCRIBE_SERVICE_INFO_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_RX_MATCH_FILTER_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_RX_MATCH_FILTER, NAN_REQ_ATTR_SUBSCRIBE_RX_MATCH_FILTER_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_TX_MATCH_FILTER_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_TX_MATCH_FILTER, NAN_REQ_ATTR_SUBSCRIBE_TX_MATCH_FILTER_LEN, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_RSSI_THRESHOLD_FLAG, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_CONN_MAP, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_NUM_INTF_ADDR_PRESENT, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_INTF_ADDR, ETH_ALEN, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_RECV_IND_CFG, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_SDEA_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_PUBLISH_SDEA, NAN_REQ_ATTR_PUBLISH_SDEA_LEN, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RANGING_AUTO_RESPONSE, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_NDP_TYPE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_SECURITY_CFG, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_RANGING_STATE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_RANGE_REPORT, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_QOS_CFG, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGING_CFG_INTERVAL, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGING_CFG_INDICATION, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGING_CFG_INGRESS_MM, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGING_CFG_EGRESS_MM, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_CIPHER_TYPE, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_SECURITY_KEY_TYPE, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_SECURITY_PMK_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SECURITY_PMK, NAN_REQ_ATTR_SECURITY_PMK_LEN, "0");
	nla_put_u32(nl_skb, NAN_REQ_ATTR_SECURITY_PASSPHRASE_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SECURITY_PASSPHRASE, NAN_REQ_ATTR_SECURITY_PASSPHRASE_LEN, "0");
	nla_put_u32(nl_skb, NAN_REQ_ATTR_SCID_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SCID, NAN_REQ_ATTR_SCID_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGE_RESPONSE_CFG_PUBLISH_ID, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_RANGE_RESPONSE_CFG_REQUESTOR_ID, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_RANGE_RESPONSE_CFG_PEER_ADDR, ETH_ALEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_RANGE_RESPONSE_CFG_RANGING_RESPONSE, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_subscribe(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_DISCONNECTING;
	KUNIT_EXPECT_EQ(test, -22, slsi_nan_subscribe(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_ID, 0);
	nlmsg_end(nl_skb, nl_hdr);
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_DISCONNECTING;
	KUNIT_EXPECT_EQ(test, -10, slsi_nan_subscribe(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_ID, 0);
	nlmsg_end(nl_skb, nl_hdr);
	ndev_vif->sta.vif_status = SLSI_VIF_STATUS_CONNECTED;
	KUNIT_EXPECT_EQ(test, -10, slsi_nan_subscribe(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_subscribe_cancel(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct net_device 	*data_dev;
	struct netdev_vif 	*data_ndev_vif;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;
	const void          *data = NULL;

	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	ndev_vif->activated = false;
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 1, slsi_nan_subscribe_cancel(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	ndev_vif->activated = true;
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_ID, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 0);
	nlmsg_end(nl_skb, nl_hdr);
	set_bit(0, ndev_vif->nan.service_id_map);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START] = kunit_kzalloc(test, sizeof(struct net_device), GFP_KERNEL);
	data_ndev_vif = netdev_priv(sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]);
	data_ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET] = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	data_ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]->valid = true;
	struct slsi_peer *peer = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, 1, slsi_nan_subscribe_cancel(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_transmit_followup(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_FOLLOWUP_ID, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_FOLLOWUP_REQUESTOR_ID, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_FOLLOWUP_ADDR, ETH_ALEN, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_FOLLOWUP_PRIORITY, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_FOLLOWUP_TX_WINDOW, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_FOLLOWUP_SERVICE_NAME_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_FOLLOWUP_SERVICE_NAME, NAN_REQ_ATTR_FOLLOWUP_SERVICE_NAME_LEN, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_FOLLOWUP_RECV_IND_CFG, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_PUBLISH_SDEA_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_PUBLISH_SDEA, NAN_REQ_ATTR_PUBLISH_SDEA_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_FOLLOWUP_SHARED_KEY_DESC_FLAG, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, -22, slsi_nan_transmit_followup(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_FOLLOWUP_ID, 1);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_FOLLOWUP_REQUESTOR_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	set_bit(0, ndev_vif->nan.service_id_map);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_transmit_followup(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(100, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_FOLLOWUP_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, -10, slsi_nan_transmit_followup(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_set_config(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;
	struct nlattr	    *nlattr_nested1, *nlattr_nested2;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SID_BEACON_VAL, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RSSI_PROXIMITY_2G4_VAL, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_MASTER_PREF, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RSSI_CLOSE_PROXIMITY_5G_VAL, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RSSI_WINDOW_SIZE_VAL, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CLUSTER_VAL, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SOCIAL_CH_SCAN_DWELL_TIME, 10, "0");
	nla_put(nl_skb, NAN_REQ_ATTR_SOCIAL_CH_SCAN_PERIOD, 10, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_RANDOM_FACTOR_FORCE_VAL, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_HOP_COUNT_FORCE_VAL, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CONN_CAPABILITY_PAYLOAD_TX, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CONN_CAPABILITY_WFD, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CONN_CAPABILITY_WFDS, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CONN_CAPABILITY_TDLS, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CONN_CAPABILITY_MESH, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CONN_CAPABILITY_IBSS, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CONN_CAPABILITY_WLAN_INFRA, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_DISCOVERY_ATTR_NUM_ENTRIES, 1);
	nlattr_nested1 = nla_nest_start(nl_skb, NAN_REQ_ATTR_DISCOVERY_ATTR_VAL);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CONN_TYPE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_NAN_ROLE, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_TRANSMIT_FREQ, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_AVAILABILITY_DURATION, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_AVAILABILITY_INTERVAL, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_MAC_ADDR_VAL, ETH_ALEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_MESH_ID_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_MESH_ID, NAN_REQ_ATTR_MESH_ID_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_INFRASTRUCTURE_SSID_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_INFRASTRUCTURE_SSID, NAN_REQ_ATTR_INFRASTRUCTURE_SSID_LEN, "0");
	nla_nest_end(nl_skb, nlattr_nested1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_FURTHER_AVAIL_NUM_ENTRIES, 2);
	nlattr_nested2 = nla_nest_start(nl_skb, NAN_REQ_ATTR_FURTHER_AVAIL_VAL);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_FURTHER_AVAIL_ENTRY_CTRL, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_FURTHER_AVAIL_CHAN_CLASS, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_FURTHER_AVAIL_CHAN, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_FURTHER_AVAIL_CHAN_MAPID, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_FURTHER_AVAIL_INTERVAL_BITMAP, 0);
	nla_nest_end(nl_skb, nlattr_nested2);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SUBSCRIBE_SID_BEACON_VAL, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_DW_2G4_INTERVAL, 2);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_DW_5G_INTERVAL, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_DISC_MAC_ADDR_RANDOM_INTERVAL, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_DISCOVERY_BEACON_INT, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_NSS, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_ENABLE_RANGING, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_DW_EARLY_TERMINATION, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_ENABLE_INSTANT_MODE, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_INSTANT_MODE_CHANNEL, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_set_config(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(10, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_set_config(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_get_capabilities(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;
	struct nlattr	    *nlattr_nested1, *nlattr_nested2;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_get_capabilities(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_data_iface_create(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;
	struct nlattr	    *nlattr_nested1, *nlattr_nested2;
	struct slsi_nan_data_iface_create_delete *intfdata = kunit_kzalloc(test, sizeof(struct slsi_nan_data_iface_create_delete), GFP_KERNEL);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, -5, slsi_nan_data_iface_create(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_DATA_INTERFACE_NAME, 6, "wlan0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	INIT_LIST_HEAD(&sdev->nan_data_interface_create_delete_data);
	INIT_WORK(&sdev->nan_data_interface_create_delete_work, slsi_nan_data_interface_create_delete_work);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_data_iface_create(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_data_iface_delete(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;
	struct nlattr	    *nlattr_nested1, *nlattr_nested2;
	struct slsi_nan_data_iface_create_delete *intfdata = kunit_kzalloc(test, sizeof(struct slsi_nan_data_iface_create_delete), GFP_KERNEL);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, -5, slsi_nan_data_iface_delete(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_DATA_INTERFACE_NAME, 6, "wlan0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 1);
	nlmsg_end(nl_skb, nl_hdr);
	INIT_LIST_HEAD(&sdev->nan_data_interface_create_delete_data);
	INIT_WORK(&sdev->nan_data_interface_create_delete_work, slsi_nan_data_interface_create_delete_work);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_data_iface_delete(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_ndp_initiate(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;
	struct nlattr	    *nlattr_nested1, *nlattr_nested2;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_REQ_INSTANCE_ID, 1);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_CHAN_REQ_TYPE, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_CHAN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_MAC_ADDR_VAL, ETH_ALEN, SLSI_DEFAULT_BSSID);
	nla_put(nl_skb, NAN_REQ_ATTR_DATA_INTERFACE_NAME, ETH_ALEN, "0");
	nla_put(nl_skb, NAN_REQ_ATTR_DATA_INTERFACE_NAME_LEN, IFNAMSIZ, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_SECURITY_CFG, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_QOS_CFG, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_APP_INFO_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_APP_INFO, NAN_REQ_ATTR_APP_INFO_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SERVICE_NAME_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SERVICE_NAME, NAN_REQ_ATTR_SERVICE_NAME_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_ndp_initiate(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
	
	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(10, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, -10, slsi_nan_ndp_initiate(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

}

static void test_nl80211_vendor_nan_slsi_nan_ndp_respond(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;
	struct nlattr	    *nlattr_nested1, *nlattr_nested2;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_NDP_INSTANCE_ID, 1);
	nla_put(nl_skb, NAN_REQ_ATTR_DATA_INTERFACE_NAME, ETH_ALEN, "0");
	nla_put(nl_skb, NAN_REQ_ATTR_DATA_INTERFACE_NAME_LEN, IFNAMSIZ, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_SECURITY_CFG, 0);
	nla_put_u8(nl_skb, NAN_REQ_ATTR_SDEA_PARAM_QOS_CFG, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_APP_INFO_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_APP_INFO, NAN_REQ_ATTR_APP_INFO_LEN, "0");
	nla_put_u8(nl_skb, NAN_REQ_ATTR_NDP_RESPONSE_CODE, 0);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_SERVICE_NAME_LEN, 0);
	nla_put(nl_skb, NAN_REQ_ATTR_SERVICE_NAME, NAN_REQ_ATTR_SERVICE_NAME_LEN, "0");
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_ndp_respond(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(10, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, -10, slsi_nan_ndp_respond(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_ndp_end(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct wiphy 		*wiphy = sdev->wiphy;
	struct wireless_dev *wdev = &ndev_vif->wdev;
	struct sk_buff	    *nl_skb;
	struct nlmsghdr	    *nl_hdr = NULL;
	struct nlattr	    *nlattr_nested1, *nlattr_nested2;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = true;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(1000, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_NDP_INSTANCE_ID, 1);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 0);
	nlmsg_end(nl_skb, nl_hdr);
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_ndp_end(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	ndev_vif->activated = false;
	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	nl_skb = nlmsg_new(10, GFP_KERNEL);
	nl_hdr = nlmsg_hdr(nl_skb);
	nlmsg_put(nl_skb, 0, 0, 0, 0, 0);
	nla_put_u32(nl_skb, NAN_REQ_ATTR_NDP_INSTANCE_ID, 1);
	nla_put_u16(nl_skb, NAN_REQ_ATTR_HAL_TRANSACTION_ID, 0);
	nlmsg_end(nl_skb, nl_hdr);
	sdev->wiphy->n_vendor_events = 50;
	KUNIT_EXPECT_EQ(test, 0, slsi_nan_ndp_end(wiphy, wdev, nlmsg_data(nl_hdr), nlmsg_len(nl_hdr)));
}

static void test_nl80211_vendor_nan_slsi_nan_event(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct sk_buff	    *skb;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	skb = fapi_alloc(mlme_nan_event_ind, MLME_NAN_EVENT_IND, SLSI_NET_INDEX_NAN, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.event, FAPI_EVENT_WIFI_EVENT_NAN_PUBLISH_TERMINATED);
	fapi_set_u16(skb, u.mlme_nan_event_ind.identifier, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.reason_code, FAPI_REASONCODE_NAN_TRANSMIT_FOLLOWUP_SUCCESS);
	fapi_set_memcpy(skb, u.mlme_nan_event_ind.address_or_identifier, SLSI_DEFAULT_HW_MAC_ADDR);
	sdev->wiphy->n_vendor_events = 50;
	slsi_nan_event(sdev, dev, skb);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	skb = fapi_alloc(mlme_nan_event_ind, MLME_NAN_EVENT_IND, SLSI_NET_INDEX_NAN, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.event, FAPI_EVENT_WIFI_EVENT_NAN_SUBSCRIBE_TERMINATED);
	fapi_set_u16(skb, u.mlme_nan_event_ind.identifier, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.reason_code, FAPI_REASONCODE_NAN_TRANSMIT_FOLLOWUP_FAILURE);
	fapi_set_memcpy(skb, u.mlme_nan_event_ind.address_or_identifier, SLSI_DEFAULT_HW_MAC_ADDR);
	set_bit(0, ndev_vif->nan.service_id_map);
	sdev->wiphy->n_vendor_events = 50;
	slsi_nan_event(sdev, dev, skb);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	skb = fapi_alloc(mlme_nan_event_ind, MLME_NAN_EVENT_IND, SLSI_NET_INDEX_NAN, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.event, FAPI_EVENT_WIFI_EVENT_NAN_MATCH_EXPIRED);
	fapi_set_u16(skb, u.mlme_nan_event_ind.identifier, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.reason_code, FAPI_REASONCODE_NAN_TRANSMIT_FOLLOWUP_SUCCESS);
	fapi_set_memcpy(skb, u.mlme_nan_event_ind.address_or_identifier, SLSI_DEFAULT_HW_MAC_ADDR);
	sdev->wiphy->n_vendor_events = 50;
	slsi_nan_event(sdev, dev, skb);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	skb = fapi_alloc(mlme_nan_event_ind, MLME_NAN_EVENT_IND, SLSI_NET_INDEX_NAN, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.event, FAPI_EVENT_WIFI_EVENT_NAN_ADDRESS_CHANGED);
	fapi_set_u16(skb, u.mlme_nan_event_ind.identifier, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.reason_code, FAPI_REASONCODE_NAN_TRANSMIT_FOLLOWUP_SUCCESS);
	fapi_set_memcpy(skb, u.mlme_nan_event_ind.address_or_identifier, SLSI_DEFAULT_HW_MAC_ADDR);
	sdev->wiphy->n_vendor_events = 50;
	slsi_nan_event(sdev, dev, skb);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	skb = fapi_alloc(mlme_nan_event_ind, MLME_NAN_EVENT_IND, SLSI_NET_INDEX_NAN, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.event, FAPI_EVENT_WIFI_EVENT_NAN_CLUSTER_STARTED);
	fapi_set_u16(skb, u.mlme_nan_event_ind.identifier, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.reason_code, FAPI_REASONCODE_NAN_TRANSMIT_FOLLOWUP_SUCCESS);
	fapi_set_memcpy(skb, u.mlme_nan_event_ind.address_or_identifier, SLSI_DEFAULT_HW_MAC_ADDR);
	sdev->wiphy->n_vendor_events = 50;
	slsi_nan_event(sdev, dev, skb);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	skb = fapi_alloc(mlme_nan_event_ind, MLME_NAN_EVENT_IND, SLSI_NET_INDEX_NAN, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.event, FAPI_EVENT_WIFI_EVENT_NAN_CLUSTER_JOINED);
	fapi_set_u16(skb, u.mlme_nan_event_ind.identifier, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.reason_code, FAPI_REASONCODE_NAN_TRANSMIT_FOLLOWUP_SUCCESS);
	fapi_set_memcpy(skb, u.mlme_nan_event_ind.address_or_identifier, SLSI_DEFAULT_HW_MAC_ADDR);
	sdev->wiphy->n_vendor_events = 50;
	slsi_nan_event(sdev, dev, skb);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	skb = fapi_alloc(mlme_nan_event_ind, MLME_NAN_EVENT_IND, SLSI_NET_INDEX_NAN, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.event, FAPI_EVENT_WIFI_EVENT_NAN_TRANSMIT_FOLLOWUP);
	fapi_set_u16(skb, u.mlme_nan_event_ind.identifier, 1);
	fapi_set_u16(skb, u.mlme_nan_event_ind.reason_code, FAPI_REASONCODE_NAN_TRANSMIT_FOLLOWUP_SUCCESS);
	fapi_set_memcpy(skb, u.mlme_nan_event_ind.address_or_identifier, SLSI_DEFAULT_HW_MAC_ADDR);
	sdev->wiphy->n_vendor_events = 50;
	slsi_nan_event(sdev, dev, skb);
}

static void test_nl80211_vendor_nan_slsi_nan_send_disabled_event(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	sdev->wiphy->n_vendor_events = 50;
	slsi_nan_send_disabled_event(sdev, dev, SLSI_HAL_NAN_STATUS_SUCCESS);
}

static void test_nl80211_vendor_nan_slsi_nan_followup_ind(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct sk_buff	    *skb;
	struct slsi_hal_nan_followup_ind *msg = kunit_kzalloc(test, sizeof(struct slsi_hal_nan_followup_ind), GFP_KERNEL);
	u8 *p;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	skb = fapi_alloc(mlme_nan_followup_ind, MLME_NAN_FOLLOWUP_IND, SLSI_NET_INDEX_NAN, 0);
	memcpy(skb->data, msg, sizeof(struct slsi_hal_nan_followup_ind));
	fapi_set_u16(skb, u.mlme_nan_followup_ind.session_id, 1);
	fapi_set_u16(skb, u.mlme_nan_followup_ind.match_id, 1);
	fapi_set_memcpy(skb, u.mlme_nan_followup_ind.peer_nan_management_interface_address, SLSI_DEFAULT_HW_MAC_ADDR);
	// SLSI_NAN_TLV_TAG_SERVICE_SPECIFIC_INFO
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_SERVICE_SPECIFIC_INFO);
	p = fapi_append_data_u16(skb, 1);
	p = fapi_append_data(skb, "1", 1);

	// SLSI_NAN_TLV_TAG_EXT_SERVICE_SPECIFIC_INFO
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_EXT_SERVICE_SPECIFIC_INFO);
	p = fapi_append_data_u16(skb, 1);
	p = fapi_append_data(skb, "1", 1);
	sdev->wiphy->n_vendor_events = 50;
	slsi_nan_followup_ind(sdev, dev, skb);
}

static void test_nl80211_vendor_nan_slsi_nan_service_ind(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct sk_buff	    *skb;
	struct slsi_hal_nan_match_ind *msg = kunit_kzalloc(test, sizeof(struct slsi_hal_nan_match_ind), GFP_KERNEL);
	u8 *p;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	skb = fapi_alloc(mlme_nan_service_ind, MLME_NAN_SERVICE_IND, SLSI_NET_INDEX_NAN, 0);
	fapi_set_u16(skb, u.mlme_nan_service_ind.session_id, 1);
	fapi_set_u16(skb, u.mlme_nan_service_ind.match_id, 1);
	// SLSI_NAN_TLV_TAG_MATCH_IND
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_MATCH_IND);
	p = fapi_append_data_u16(skb, 0x15);
	p = fapi_append_data(skb, SLSI_DEFAULT_BSSID, ETH_ALEN);
	p = fapi_append_data_u16(skb, 0);
	p = fapi_append_data_u16(skb, 0);
	p = fapi_append_data_u8(skb, 0);
	p = fapi_append_data_u8(skb, 0);
	p = fapi_append_data_u16(skb, 0);
	p = fapi_append_data_u16(skb, 0);
	p = fapi_append_data_u32(skb, 0);
	p = fapi_append_data_u8(skb, 0);

	// SLSI_NAN_TLV_TAG_SERVICE_SPECIFIC_INFO
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_SERVICE_SPECIFIC_INFO);
	p = fapi_append_data_u16(skb, 0);

	// SLSI_NAN_TLV_TAG_EXT_SERVICE_SPECIFIC_INFO
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_EXT_SERVICE_SPECIFIC_INFO);
	p = fapi_append_data_u16(skb, 0);

	// SLSI_NAN_TLV_TAG_DATA_PATH_SECURITY
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_DATA_PATH_SECURITY);
	p = fapi_append_data_u16(skb, 0);

	// SLSI_NAN_TLV_TAG_MATCH_FILTER
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_MATCH_FILTER);
	p = fapi_append_data_u16(skb, 0);

	// SLSI_NAN_TLV_NAN_PAIRING_CONFIG
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_NAN_PAIRING_CONFIG);
	p = fapi_append_data_u16(skb, 0);
	sdev->wiphy->n_vendor_events = 50;
	slsi_nan_service_ind(sdev, dev, skb);
}

static void test_nl80211_vendor_nan_slsi_nan_ndp_setup_ind(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct netdev_vif 	*ndev_data_vif;
	struct sk_buff		*skb;
	u8 *p;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	sdev->wiphy->n_vendor_events = 50;
	skb = fapi_alloc(mlme_ndp_request_ind, MLME_NDP_REQUEST_IND, SLSI_NET_INDEX_NAN, 0);
	fapi_set_memcpy(skb, u.mlme_ndp_request_ind.peer_ndp_interface_address, SLSI_DEFAULT_HW_MAC_ADDR);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.result_code, FAPI_RESULTCODE_NDP_REJECTED);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.ndp_instance_id, 0);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.flow_id, 1);
	slsi_nan_ndp_setup_ind(sdev, dev, skb, true);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	sdev->wiphy->n_vendor_events = 50;
	skb = fapi_alloc(mlme_ndp_request_ind, MLME_NDP_REQUEST_IND, SLSI_NET_INDEX_NAN, 0);
	fapi_set_memcpy(skb, u.mlme_ndp_request_ind.peer_ndp_interface_address, SLSI_DEFAULT_HW_MAC_ADDR);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.result_code, FAPI_RESULTCODE_NAN_NO_OTA_ACK);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.ndp_instance_id, 0);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.flow_id, 1);
	slsi_nan_ndp_setup_ind(sdev, dev, skb, true);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	sdev->wiphy->n_vendor_events = 50;
	skb = fapi_alloc(mlme_ndp_request_ind, MLME_NDP_REQUEST_IND, SLSI_NET_INDEX_NAN, 0);
	fapi_set_memcpy(skb, u.mlme_ndp_request_ind.peer_ndp_interface_address, SLSI_DEFAULT_HW_MAC_ADDR);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.result_code, FAPI_RESULTCODE_NDL_UNACCEPTABLE);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.ndp_instance_id, 0);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.flow_id, 1);
	slsi_nan_ndp_setup_ind(sdev, dev, skb, true);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	sdev->wiphy->n_vendor_events = 50;
	skb = fapi_alloc(mlme_ndp_request_ind, MLME_NDP_REQUEST_IND, SLSI_NET_INDEX_NAN, 0);
	fapi_set_memcpy(skb, u.mlme_ndp_request_ind.peer_ndp_interface_address, SLSI_DEFAULT_HW_MAC_ADDR);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.result_code, FAPI_RESULTCODE_SUCCESS);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.ndp_instance_id, 1);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.flow_id, 1);
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_APP_INFO);
	p = fapi_append_data_u16(skb, 0);
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_WFA_SERVICE_INFO);
	p = fapi_append_data_u16(skb, 2);
	p = fapi_append_data_u16(skb, 2);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START] = kunit_kzalloc(test, sizeof(struct net_device), GFP_KERNEL);
	ndev_data_vif = netdev_priv(sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]);
	SLSI_MUTEX_INIT(ndev_data_vif->vif_mutex);
	memcpy(ndev_vif->nan.ndp_ndi[0], SLSI_DEFAULT_BSSID, 6);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]->dev_addr = SLSI_DEFAULT_BSSID;
	slsi_nan_ndp_setup_ind(sdev, dev, skb, true);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	sdev->wiphy->n_vendor_events = 50;
	skb = fapi_alloc(mlme_ndp_request_ind, MLME_NDP_REQUEST_IND, SLSI_NET_INDEX_NAN, 0);
	fapi_set_memcpy(skb, u.mlme_ndp_request_ind.peer_ndp_interface_address, SLSI_DEFAULT_HW_MAC_ADDR);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.result_code, FAPI_RESULTCODE_NDP_REJECTED);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.ndp_instance_id, 1);
	fapi_set_u16(skb, u.mlme_ndp_request_ind.flow_id, 1);
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_APP_INFO);
	p = fapi_append_data_u16(skb, 0);
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_WFA_SERVICE_INFO);
	p = fapi_append_data_u16(skb, 2);
	p = fapi_append_data_u16(skb, 2);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START] = kunit_kzalloc(test, sizeof(struct net_device), GFP_KERNEL);
	ndev_data_vif = netdev_priv(sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]);
	ndev_data_vif->peer_sta_record[0] = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	ndev_data_vif->ifnum = 10;
	SLSI_MUTEX_INIT(ndev_data_vif->vif_mutex);
	memcpy(ndev_vif->nan.ndp_ndi[0], SLSI_DEFAULT_BSSID, 6);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]->dev_addr = SLSI_DEFAULT_BSSID;
	sdev->nan_max_ndp_instances = 0x000f;
	ndev_vif->nan.ndp_instance_id2ndl_vif[0] = 1;
	ndev_vif->activated = true;
	sdev->device_config.user_suspend_mode = 1;
	slsi_nan_ndp_setup_ind(sdev, dev, skb, false);
}

static void test_nl80211_vendor_nan_slsi_nan_ndp_requested_ind(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct netdev_vif 	*ndev_data_vif;
	struct sk_buff		*skb;
	u8 *p;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	sdev->wiphy->n_vendor_events = 50;
	skb = fapi_alloc(mlme_ndp_requested_ind, MLME_NDP_REQUESTED_IND, SLSI_NET_INDEX_NAN, 64);
	fapi_set_u16(skb, u.mlme_ndp_requested_ind.session_id, 1);
	fapi_set_u16(skb, u.mlme_ndp_requested_ind.request_id, 1);
	fapi_set_u16(skb, u.mlme_ndp_requested_ind.security_required, 1);
	fapi_set_memcpy(skb, u.mlme_ndp_requested_ind.peer_nan_management_interface_address, SLSI_DEFAULT_HW_MAC_ADDR);
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_RANGING);
	p = fapi_append_data_u16(skb, 1);
	p = fapi_append_data(skb, "1", 1);
	slsi_nan_ndp_requested_ind(sdev, dev, skb);
	
	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	sdev->wiphy->n_vendor_events = 50;
	skb = fapi_alloc(mlme_ndp_requested_ind, MLME_NDP_REQUESTED_IND, SLSI_NET_INDEX_NAN, 64);
	fapi_set_u16(skb, u.mlme_ndp_requested_ind.session_id, 1);
	fapi_set_u16(skb, u.mlme_ndp_requested_ind.request_id, 1);
	fapi_set_u16(skb, u.mlme_ndp_requested_ind.security_required, 1);
	fapi_set_memcpy(skb, u.mlme_ndp_requested_ind.peer_nan_management_interface_address, SLSI_DEFAULT_HW_MAC_ADDR);
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_TAG_APP_INFO);
	p = fapi_append_data_u16(skb, 1);
	p = fapi_append_data(skb, "1", 1);
	ndev_vif->nan.next_ndp_instance_id = SLSI_NAN_MAX_NDP_INSTANCES + 1;
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START] = kunit_kzalloc(test, sizeof(struct net_device), GFP_KERNEL);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]->dev_addr = SLSI_DEFAULT_HW_MAC_ADDR;
	ndev_data_vif = netdev_priv(sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]);
	ndev_data_vif->peer_sta_record[0] = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	ndev_data_vif->peer_sta_record[0]->valid = true;
	slsi_nan_ndp_requested_ind(sdev, dev, skb);
}

static void test_nl80211_vendor_nan_slsi_nan_ndp_termination_ind(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct netdev_vif 	*ndev_data_vif;
	struct slsi_peer	*peer = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	struct sk_buff		*skb;
	u8 *p;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	sdev->wiphy->n_vendor_events = 50;
	skb = fapi_alloc(mlme_ndp_terminated_ind, MLME_NDP_TERMINATED_IND, SLSI_NET_INDEX_NAN, 64);
	fapi_set_u16(skb, u.mlme_ndp_terminated_ind.ndp_instance_id, 1);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START] = kunit_kzalloc(test, sizeof(struct net_device), GFP_KERNEL);
	ndev_data_vif = netdev_priv(sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]);
	ndev_data_vif->peer_sta_record[0] = kunit_kzalloc(test, sizeof(struct slsi_peer), GFP_KERNEL);
	ndev_data_vif->peer_sta_record[0]->valid = true;
	ndev_data_vif->peer_sta_record[0]->ndp_count = 1;
	ndev_data_vif->nan.ndp_count = 1;
	SLSI_MUTEX_INIT(ndev_data_vif->vif_mutex);
	memcpy(ndev_vif->nan.ndp_ndi[0], SLSI_DEFAULT_BSSID, 6);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START]->dev_addr = SLSI_DEFAULT_BSSID;
	ndev_vif->nan.ndp_active_id_map = 2;
	sdev->nan_max_ndp_instances = 0x000f;
	ndev_vif->nan.ndp_instance_id2ndl_vif[0] = 1;
	slsi_nan_ndp_termination_ind(sdev, dev, skb);
}

static void test_nl80211_vendor_nan_slsi_send_nan_range_config(struct kunit *test)
{
	struct slsi_dev        *sdev = TEST_TO_SDEV(test);
	struct net_device      *dev = TEST_TO_DEV(test);
	struct netdev_vif      *ndev_vif = netdev_priv(dev);
	struct slsi_rtt_config *nl_rtt_params = kunit_kzalloc(test, sizeof(struct slsi_rtt_config), GFP_KERNEL);
	int rtt_id = 0;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	ndev_vif->activated = false;
	slsi_send_nan_range_config(sdev, 1, nl_rtt_params, rtt_id);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	ndev_vif->activated = true;
	slsi_send_nan_range_config(sdev, 1, nl_rtt_params, rtt_id);
}

static void test_nl80211_vendor_nan_slsi_send_nan_range_cancel(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	ndev_vif->activated = false;
	slsi_send_nan_range_cancel(sdev);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	ndev_vif->activated = true;
	slsi_send_nan_range_cancel(sdev);
}

static void test_nl80211_vendor_nan_slsi_rx_nan_range_ind(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct sk_buff		*skb;
	int rtt_id = 1;
	u8 *p;

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	sdev->rtt_id_params[rtt_id - 1] = kunit_kzalloc(test, sizeof(struct slsi_rtt_id_params), GFP_KERNEL);
	sdev->rtt_id_params[rtt_id - 1]->peer_type = SLSI_RTT_PEER_NAN;
	sdev->rtt_id_params[rtt_id - 1]->hal_request_id = 1;
	skb = fapi_alloc(mlme_nan_range_ind, MLME_NAN_RANGE_IND, SLSI_NET_INDEX_NAN, 64);
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_NAN_RTT_RESULT);
	p = fapi_append_data_u16(skb, SLSI_NAN_TLV_NAN_RTT_RESULT_LEN);
	p = fapi_append_data(skb, SLSI_DEFAULT_BSSID, ETH_ALEN);
	p = fapi_append_data_u16(skb, 0x0001);
	p = fapi_append_data_u8(skb, 0);
	p = fapi_append_data_u8(skb, 0);
	p = fapi_append_data_u8(skb, 0);
	p = fapi_append_data_u8(skb, 0);
	p = fapi_append_data_u32(skb, 0);
	p = fapi_append_data_u16(skb, 0);
	p = fapi_append_data_u16(skb, 0);
	p = fapi_append_data_u32(skb, 0);
	p = fapi_append_data_u32(skb, 0);
	p = fapi_append_data_u32(skb, 0);
	p = fapi_append_data_u8(skb, 0);
	sdev->wiphy->n_vendor_events = 50;
	slsi_rx_nan_range_ind(sdev, dev, skb);
}

static void test_nl80211_vendor_nan_slsi_nan_data_interface_create_delete_work(struct kunit *test)
{
	struct net_device  *dev = TEST_TO_DEV(test);
	struct slsi_dev    *sdev = TEST_TO_SDEV(test);
	struct net_device  *dev_ndp;
	struct work_struct *work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	struct slsi_nan_data_iface_create_delete *intfdata = NULL;
	u16 transaction_id = 0;
	u8 iface_name[IFNAMSIZ] = "TEST";

	sdev->nan_data_interface_create_delete_work = *work;
	//sdev = container_of(work, struct slsi_dev, nan_data_interface_create_delete_work);
	sdev->wiphy->n_vendor_events = 50;
	mutex_init(&sdev->wiphy->mtx);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	INIT_LIST_HEAD(&sdev->nan_data_interface_create_delete_data);
	intfdata = kzalloc(sizeof(*intfdata), GFP_KERNEL);
	memcpy(intfdata->ifname, iface_name, strlen(iface_name));
	intfdata->transaction_id = transaction_id;
	intfdata->iface_action = NAN_IFACE_CREATE;
	list_add(&intfdata->list, &sdev->nan_data_interface_create_delete_data);
	sdev->netdev[SLSI_NAN_DATA_IFINDEX_START] = kunit_kzalloc(test, sizeof(struct net_device), GFP_KERNEL);
	dev_ndp = sdev->netdev[SLSI_NAN_DATA_IFINDEX_START];
	slsi_nan_data_interface_create_delete_work(&sdev->nan_data_interface_create_delete_work);

	sdev->nan_data_interface_create_delete_work = *work;
	//sdev = container_of(work, struct slsi_dev, nan_data_interface_create_delete_work);
	sdev->wiphy->n_vendor_events = 50;
	mutex_init(&sdev->wiphy->mtx);
	SLSI_MUTEX_INIT(sdev->netdev_add_remove_mutex);
	INIT_LIST_HEAD(&sdev->nan_data_interface_create_delete_data);
	intfdata = kzalloc(sizeof(*intfdata), GFP_KERNEL);
	memcpy(intfdata->ifname, iface_name, strlen(iface_name));
	intfdata->transaction_id = transaction_id;
	intfdata->iface_action = NAN_IFACE_CREATE;
	list_add(&intfdata->list, &sdev->nan_data_interface_create_delete_data);
	slsi_nan_data_interface_create_delete_work(&sdev->nan_data_interface_create_delete_work);
}

static void test_nl80211_vendor_nan_slsi_vendor_nan_set_command_event_ind(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct net_device   *dev = TEST_TO_DEV(test);
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct sk_buff		*skb = kunit_kzalloc(test, sizeof(struct sk_buff), GFP_KERNEL);

	SLSI_MUTEX_INIT(ndev_vif->vif_mutex);
	sdev->wiphy->n_vendor_events = 50;
	slsi_vendor_nan_set_command_event_ind(sdev, dev, skb);
}

static void test_nl80211_vendor_nan_slsi_vendor_nan_prev_cmd_data_free(struct kunit *test)
{
	struct slsi_dev     *sdev = TEST_TO_SDEV(test);
	struct slsi_hal_nan_vendor_prev_cmd_data_info *prev_cmd_data_info;

	INIT_LIST_HEAD(&sdev->slsi_hal_nan_vendor_prev_cmd_data);
	prev_cmd_data_info = kunit_kzalloc(test, sizeof(*prev_cmd_data_info), GFP_KERNEL);
	list_add(&prev_cmd_data_info->list, &sdev->slsi_hal_nan_vendor_prev_cmd_data);
	slsi_vendor_nan_prev_cmd_data_free(sdev);
}

static int nl80211_vendor_nan_test_init(struct kunit *test)
{
	test_nan_dev_init(test);

	kunit_log(KERN_INFO, test, "%s: initialized.", __func__);
	return 0;
}

static void nl80211_vendor_nan_test_exit(struct kunit *test)
{
	kunit_log(KERN_INFO, test, "%s: completed.", __func__);
}

static struct kunit_case nl80211_vendor_nan_test_cases[] = {
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_dump_vif_data),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_enable),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_disable),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_publish),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_publish_cancel),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_subscribe),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_subscribe_cancel),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_transmit_followup),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_set_config),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_get_capabilities),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_data_iface_create),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_data_iface_delete),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_ndp_initiate),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_ndp_respond),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_ndp_end),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_event),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_send_disabled_event),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_followup_ind),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_service_ind),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_ndp_setup_ind),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_ndp_requested_ind),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_ndp_termination_ind),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_send_nan_range_config),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_send_nan_range_cancel),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_rx_nan_range_ind),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_nan_data_interface_create_delete_work),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_vendor_nan_set_command_event_ind),
	KUNIT_CASE(test_nl80211_vendor_nan_slsi_vendor_nan_prev_cmd_data_free),
	{}
};

static struct kunit_suite nl80211_vendor_nan_test_suite[] = {
	{
		.name = "kunit-nl80211_vendor_nan-test",
		.test_cases = nl80211_vendor_nan_test_cases,
		.init = nl80211_vendor_nan_test_init,
		.exit = nl80211_vendor_nan_test_exit,
	}
};

kunit_test_suites(nl80211_vendor_nan_test_suite);
