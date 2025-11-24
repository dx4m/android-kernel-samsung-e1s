/*****************************************************************************
 *
 * Copyright (c) 2014 - 2023 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#include "cfg80211_ops.h"
#include "debug.h"
#include "mgt.h"
#include "mlme.h"
#include "ba.h"
#ifdef CONFIG_SCSC_WLAN_TX_API
#include "tx_api.h"
#endif
#include <pcie_scsc/scsc_warn.h>

struct net_device *slsi_nan_get_netdev(struct slsi_dev *sdev)
{
#if CONFIG_SCSC_WLAN_MAX_INTERFACES >= SLSI_NET_INDEX_NAN
	return slsi_get_netdev(sdev, SLSI_NET_INDEX_NAN);
#else
	return NULL;
#endif
}

char *slsi_nan_convert_byte_to_string(int length, u8 *byte_string)
{
	static char info_string[61] = {0};
	int len = length > 20 ? 20 : length; /* only first 20bytes needs to be printed. */
	int i = 0, max_size = 61, slen = 0;

	for (i = 0; i < len && slen < max_size - 3; i++)
		slen += snprintf(&info_string[slen], 61 - slen, "%02x ", byte_string[i]);
	info_string[slen] = '\0';
	return info_string;
}

unsigned int slsi_count_setbits_u16(u16 n)
{
	unsigned int count = 0;

	while (n) {
		count += n & 1;
		n >>= 1;
	}
	return count;
}

void slsi_nan_dump_vif_data(struct slsi_dev *sdev, struct netdev_vif *ndev_vif)
{
	struct slsi_vif_nan nan = ndev_vif->nan;
	struct slsi_nan_discovery_info *disc_info = nan.disc_info;
	int i = 0;

	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	for (i = 0; i < BITS_TO_LONGS(SLSI_NAN_MAX_SERVICE_ID); i++)
		SLSI_INFO(sdev, "Service Id_Map[%d]: 0x%x\n", i, nan.service_id_map[i]);

	SLSI_INFO(sdev, "followup_id_map: 0x%x, ndp_instance_id_map: 0x%x\n",
		  nan.followup_id_map, nan.ndp_instance_id_map);
	SLSI_INFO(sdev, "next_ndp_instance_id: 0x%x\n", nan.next_ndp_instance_id);
	for (i = 0; i < SLSI_NAN_MAX_NDP_INSTANCES; i++) {
		SLSI_INFO(sdev, "NDL List peer_nmi: " MACSTR ", ndp_count:%d\n", MAC2STR(nan.ndl_list[i].peer_nmi),
			  nan.ndl_list[i].ndp_count);
		SLSI_INFO(sdev, "ndp_ndi: " MACSTR ", ndp_instance_id2ndl_vif: %d\n", MAC2STR(nan.ndp_ndi[i]), nan.ndp_instance_id2ndl_vif[i]);
		SLSI_INFO(sdev, "ndp_local_ndp_instance_id: %d, ndp_state: %d\n", nan.ndp_local_ndp_instance_id[i], nan.ndp_state[i]);
	}
	SLSI_INFO(sdev, "disable_cluster_merge: %d\n", nan.disable_cluster_merge);
	for (i = 0; i < SLSI_NAN_MAX_SERVICE_ID + 1; i++)
		SLSI_INFO(sdev, "nan_sdf_flags: %d\n", nan.nan_sdf_flags[i]);
	SLSI_INFO(sdev, "local_nmi: " MACSTR ", cluster_id: " MACSTR "\n",
		  MAC2STR(nan.local_nmi), MAC2STR(sdev->nan_cluster_id));
	SLSI_INFO(sdev, "state: %d, role: %d, operating_channel: %d, %d\n", nan.state, nan.role,
		  nan.operating_channel[0], nan.operating_channel[1]);
	SLSI_INFO(sdev, "master_pref_value: %d, amr: 0x%08x%08x, hopcount: %d\n",
		  nan.master_pref_value, nan.amr_higher, nan.amr_lower, nan.hopcount);
	SLSI_INFO(sdev, "random_mac_interval_sec: %d\n", nan.random_mac_interval_sec);
	SLSI_INFO(sdev, "matchid: %d\n", nan.matchid);
	while (disc_info) {
		SLSI_INFO(sdev, "Disc_Info peer_addr: " MACSTR ", session_id: %d, match_id: %d\n", MAC2STR(disc_info->peer_addr),
			  disc_info->session_id, disc_info->match_id);
		disc_info = disc_info->next;
	}
}

static void slsi_nan_dump_ndp_peer_info(struct slsi_dev *sdev, struct net_device *nan_mgmt_dev,
					struct net_device *data_dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(data_dev);
	int i;
	struct slsi_peer *peer;
	u32 ndp_instance_id;

	for (i = 0; i < SLSI_NAN_MAX_NDP_INSTANCES + 1; i++) {
		peer = ndev_vif->peer_sta_record[i];
		if (peer && peer->valid) {
			ndp_instance_id = slsi_nan_get_ndp_from_ndl_local_ndi(nan_mgmt_dev, peer->ndl_vif, data_dev->dev_addr);
			SLSI_INFO(sdev, "PeerIdx:%d, ndpInstId:%d, ndl_vif:%d, peer ndpCount:%d\n",
				  i, ndp_instance_id, peer->ndl_vif, peer->ndp_count);
		}
	}
}

static void slsi_nan_dump_ndp_data(struct slsi_dev *sdev, struct net_device *nan_mgmt_dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(nan_mgmt_dev);
	struct net_device *data_dev;
	struct slsi_nan_ndl_info *ndl_list = ndev_vif->nan.ndl_list;
	int totalndpcount = 0;
	int i;

	totalndpcount = slsi_count_setbits_u16(ndev_vif->nan.ndp_active_id_map);
	SLSI_INFO(sdev, "NDP MAP:0x%08x NDP Count %d\n", ndev_vif->nan.ndp_instance_id_map, totalndpcount);
	SLSI_INFO(sdev,
		  "ndl_list[0-3]:[" MACSTR ",%d][" MACSTR ",%d][" MACSTR ",%d][" MACSTR ",%d]\n",
		  MAC2STR(ndl_list[0].peer_nmi), ndl_list[0].ndp_count,
		  MAC2STR(ndl_list[1].peer_nmi), ndl_list[1].ndp_count,
		  MAC2STR(ndl_list[2].peer_nmi), ndl_list[2].ndp_count,
		  MAC2STR(ndl_list[3].peer_nmi), ndl_list[3].ndp_count);
	SLSI_INFO(sdev,
		  "ndl_list[4-7]:[" MACSTR ",%d][" MACSTR ",%d][" MACSTR ",%d][" MACSTR ",%d]\n",
		  MAC2STR(ndl_list[4].peer_nmi), ndl_list[4].ndp_count,
		  MAC2STR(ndl_list[5].peer_nmi), ndl_list[5].ndp_count,
		  MAC2STR(ndl_list[6].peer_nmi), ndl_list[6].ndp_count,
		  MAC2STR(ndl_list[7].peer_nmi), ndl_list[7].ndp_count);
	SLSI_INFO(sdev,
		  "ndp_ndi table[0-3]: [" MACSTR "][" MACSTR "][" MACSTR "][" MACSTR "]\n",
		  MAC2STR(ndev_vif->nan.ndp_ndi[0]), MAC2STR(ndev_vif->nan.ndp_ndi[1]),
		  MAC2STR(ndev_vif->nan.ndp_ndi[2]), MAC2STR(ndev_vif->nan.ndp_ndi[3]));
	SLSI_INFO(sdev,
		  "ndp_ndi table[4-7]: [" MACSTR "][" MACSTR "][" MACSTR "][" MACSTR "]\n",
		  MAC2STR(ndev_vif->nan.ndp_ndi[4]), MAC2STR(ndev_vif->nan.ndp_ndi[5]),
		  MAC2STR(ndev_vif->nan.ndp_ndi[6]), MAC2STR(ndev_vif->nan.ndp_ndi[7]));
	SLSI_INFO(sdev, "ndp_instance_id2ndl_vif: [%d, %d, %d, %d, %d, %d, %d, %d]\n",
		  ndev_vif->nan.ndp_instance_id2ndl_vif[0], ndev_vif->nan.ndp_instance_id2ndl_vif[1],
		  ndev_vif->nan.ndp_instance_id2ndl_vif[2], ndev_vif->nan.ndp_instance_id2ndl_vif[3],
		  ndev_vif->nan.ndp_instance_id2ndl_vif[4], ndev_vif->nan.ndp_instance_id2ndl_vif[5],
		  ndev_vif->nan.ndp_instance_id2ndl_vif[6], ndev_vif->nan.ndp_instance_id2ndl_vif[7]);
	for (i = SLSI_NAN_DATA_IFINDEX_START; i < CONFIG_SCSC_WLAN_MAX_INTERFACES + 1; i++) {
		data_dev = slsi_get_netdev_locked(sdev, i);
		if (!data_dev)
			continue;
		slsi_nan_dump_ndp_peer_info(sdev, nan_mgmt_dev, data_dev);
	}
}

static int slsi_nan_get_new_id(u32 id_map, int max_ids, int start_idx)
{
	int i;

	if (start_idx > max_ids)
		return 0;

	if (!start_idx)
		start_idx = 1;

	for (i = start_idx; i <= max_ids; i++) {
		if (!(id_map & BIT(i)))
			return i;
	}
	for (i = 1; i < start_idx; i++) {
		if (!(id_map & BIT(i)))
			return i;
	}
	return 0;
}

static int slsi_nan_get_new_publish_subscribe_id(struct netdev_vif *ndev_vif)
{
	int new_id;

	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	new_id = find_first_zero_bit(ndev_vif->nan.service_id_map, SLSI_NAN_MAX_SERVICE_ID);
	if (new_id >= SLSI_NAN_MAX_SERVICE_ID) {
		SLSI_ERR(ndev_vif->sdev, "Publish/Subscribe reached maximum services\n");
		return 0;
	}

	return (new_id + 1);
}

static int slsi_nan_get_new_ndp_instance_id(struct netdev_vif *ndev_vif)
{
	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	return slsi_nan_get_new_id(ndev_vif->nan.ndp_instance_id_map, SLSI_NAN_MAX_NDP_INSTANCES,
				   ndev_vif->nan.next_ndp_instance_id);
}

static bool slsi_nan_is_publish_subscribe_id_active(struct netdev_vif *ndev_vif, u32 id)
{
	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	return test_bit(id - 1, ndev_vif->nan.service_id_map);
}

#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
static int slsi_nan_get_new_bootstrapping_instance_id(struct netdev_vif *ndev_vif)
{
	int new_id;

	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	new_id = find_first_zero_bit(ndev_vif->nan.bootstrapping_instance_id_map,
				     SLSI_NAN_MAX_BOOTSTRAPPING_INSTANCES);
	if (new_id >= SLSI_NAN_MAX_BOOTSTRAPPING_INSTANCES) {
		SLSI_ERR(ndev_vif->sdev, "Bootstrapping reached maximum services\n");
		return 0;
	}

	return (new_id + 1);
}

static int slsi_nan_get_new_pairing_instance_id(struct netdev_vif *ndev_vif)
{
	int new_id;

	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	new_id = find_first_zero_bit(ndev_vif->nan.pairing_instance_id_map, SLSI_NAN_MAX_PAIRING_INSTANCES);
	if (new_id >= SLSI_NAN_MAX_PAIRING_INSTANCES) {
		SLSI_ERR(ndev_vif->sdev, "Pairing reached maximum services\n");
		return 0;
	}

	return (new_id + 1);
}

static bool slsi_nan_is_bootstrapping_instance_id_active(struct netdev_vif *ndev_vif, u32 id)
{
	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	return test_bit(id - 1, ndev_vif->nan.bootstrapping_instance_id_map);
}
#endif

#ifdef SCSC_SEP_VERSION
static int slsi_set_mib_fast_connect_enabled(struct slsi_dev *sdev, bool enable)
{
	struct slsi_mib_data mib_data = { 0, NULL };
	int error = SLSI_MIB_STATUS_FAILURE;

	if (slsi_mib_encode_bool(&mib_data, SLSI_PSID_UNIFI_NAN_FAST_CONNECT_ENABLED,
				 enable, 0) != SLSI_MIB_STATUS_SUCCESS || !mib_data.dataLength )
		return error;
	error = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);
        if (error)
		SLSI_ERR(sdev, "Err Setting MIB failed. error = %d\n", error);
	kfree(mib_data.data);
	return error;
}
#endif

static void slsi_nan_pre_check(struct slsi_dev *sdev, struct net_device *dev, int *ret, int *reply_status)
{
	*ret = WIFI_HAL_SUCCESS;
	*reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;
	if (!dev) {
		SLSI_ERR(sdev, "No NAN interface\n");
		*ret = WIFI_HAL_ERROR_NOT_SUPPORTED;
		*reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
	}

	if (!slsi_dev_nan_supported(sdev)) {
		SLSI_ERR(sdev, "NAN not allowed(mib:%d)\n", sdev->nan_enabled);
		*ret = WIFI_HAL_ERROR_NOT_SUPPORTED;
		*reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
	}
}

int slsi_nan_ndp_new_entry(struct slsi_dev *sdev, struct net_device *dev, u32 ndp_instance_id,
			   u16 ndl_vif_id, const u8 *local_ndi, u8 *peer_nmi)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u16 ndl_id;
	u16 nan_data_vif_index_start = SLSI_NAN_DATA_VIF_NUM_START(sdev);

	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (ndl_vif_id < nan_data_vif_index_start ||
	    ndl_vif_id >= nan_data_vif_index_start + SLSI_NAN_MAX_NDP_INSTANCES) {
		SLSI_ERR(sdev, "Invalid ndl_vif:%d\n", ndl_vif_id);
		return 1;
	}

	if (ndp_instance_id == 0 || ndp_instance_id > SLSI_NAN_MAX_NDP_INSTANCES) {
		SLSI_ERR(sdev, "Invalid ndp:%d\n", ndp_instance_id);
		return 1;
	}

	ndl_id = ndl_vif_id - nan_data_vif_index_start;
	if (ndev_vif->nan.ndl_list[ndl_id].ndp_count < 0 ||
	    ndev_vif->nan.ndl_list[ndl_id].ndp_count > SLSI_NAN_MAX_NDP_INSTANCES) {
		SLSI_WARN(sdev, "improper ndp count(%d) for vif_id(%d)\n",
			  ndev_vif->nan.ndl_list[ndl_id].ndp_count, ndl_vif_id);
	}

	ndev_vif->nan.ndp_instance_id_map |= (u32)BIT(ndp_instance_id);
	if (peer_nmi)
		ether_addr_copy(ndev_vif->nan.ndl_list[ndl_id].peer_nmi, peer_nmi);
	ndev_vif->nan.ndl_list[ndl_id].ndp_count++;
	if (local_ndi)
		ether_addr_copy(ndev_vif->nan.ndp_ndi[ndp_instance_id - 1], local_ndi);
	ndev_vif->nan.ndp_instance_id2ndl_vif[ndp_instance_id - 1] = ndl_vif_id;
	ndev_vif->nan.ndp_state[ndp_instance_id - 1] = ndp_slot_status_in_use;
	return 0;
}

void slsi_nan_ndp_del_entry(struct slsi_dev *sdev, struct net_device *dev, u32 ndp_instance_id, const bool ndl_vif_locked)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct netdev_vif *ndev_data_vif;
	struct net_device *data_dev;
	u16 ndl_vif_id, ndl_id;
	u16 nan_data_vif_index_start = SLSI_NAN_DATA_VIF_NUM_START(sdev);

	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(sdev->netdev_add_remove_mutex));
	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (ndp_instance_id == 0 || ndp_instance_id > SLSI_NAN_MAX_NDP_INSTANCES) {
		SLSI_WARN(sdev, "Invalid ndp Instance Id:%d\n", ndp_instance_id);
		return;
	}

	ndl_vif_id = ndev_vif->nan.ndp_instance_id2ndl_vif[ndp_instance_id - 1];
	if (ndl_vif_id < nan_data_vif_index_start ||
	    ndl_vif_id >= nan_data_vif_index_start + SLSI_NAN_MAX_NDP_INSTANCES) {
		SLSI_WARN(sdev, "Invalid ndl_vif:%d\n", ndl_vif_id);
		return;
	}

	ndl_id = ndl_vif_id - nan_data_vif_index_start;
	ndev_vif->nan.ndp_instance_id_map &= ~(u32)BIT(ndp_instance_id);
	ndev_vif->nan.ndl_list[ndl_id].ndp_count--;
	data_dev = slsi_get_netdev_by_mac_addr_locked(sdev, ndev_vif->nan.ndp_ndi[ndp_instance_id - 1],
						      SLSI_NAN_DATA_IFINDEX_START);
	if (data_dev) {
		ndev_data_vif = netdev_priv(data_dev);
		if (!ndl_vif_locked)
			SLSI_MUTEX_LOCK(ndev_data_vif->vif_mutex);
		else
			WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_data_vif->vif_mutex));

		if (ndev_data_vif->nan.ndp_count == 0){
			ndev_data_vif->activated = false;
			slsi_release_dp_resources(sdev, data_dev, ndev_data_vif);
			slsi_rx_ba_update_timer(sdev, data_dev, SLSI_RX_BA_EVENT_VIF_TERMINATED);
		}
		if (!ndl_vif_locked)
			SLSI_MUTEX_UNLOCK(ndev_data_vif->vif_mutex);
	}
	if (ndev_vif->nan.ndl_list[ndl_id].ndp_count == 0)
		slsi_eth_zero_addr(ndev_vif->nan.ndl_list[ndl_id].peer_nmi);
	ndev_vif->nan.ndp_instance_id2ndl_vif[ndp_instance_id - 1] = 0;
	slsi_eth_zero_addr(ndev_vif->nan.ndp_ndi[ndp_instance_id - 1]);
	if (ndev_vif->nan.ndl_list[ndl_id].ndp_count < 0)
		SLSI_WARN(sdev, "ndp_count is negative %d for ndl idx %d\n",
			  ndev_vif->nan.ndl_list[ndl_id].ndp_count, ndl_id);
	ndev_vif->nan.ndp_state[ndp_instance_id - 1] = ndp_slot_status_free;
}

u16 slsi_nan_ndp_get_ndl_vif_id(struct slsi_dev *sdev, u8 *peer_mni, struct slsi_nan_ndl_info *ndl_list)
{

	u16 i;
	u16 nan_data_vif_index_start = SLSI_NAN_DATA_VIF_NUM_START(sdev);
	u16 free_idx = SLSI_NAN_MAX_NDP_INSTANCES + nan_data_vif_index_start;

	for (i = 0; i < SLSI_NAN_MAX_NDP_INSTANCES; i++) {
		if (ether_addr_equal(peer_mni, ndl_list[i].peer_nmi))
			return i + nan_data_vif_index_start;
		if (free_idx == SLSI_NAN_MAX_NDP_INSTANCES + nan_data_vif_index_start &&
			ndl_list[i].ndp_count == 0)
			free_idx = i + nan_data_vif_index_start;
	}
	return free_idx;
}

void slsi_nan_get_mac(struct slsi_dev *sdev, char *nan_mac_addr)
{
	memset(nan_mac_addr, 0, ETH_ALEN);
#if CONFIG_SCSC_WLAN_MAX_INTERFACES >= SLSI_NET_INDEX_NAN
	if (slsi_dev_nan_supported(sdev))
		ether_addr_copy(nan_mac_addr, sdev->netdev_addresses[SLSI_NET_INDEX_NAN]);
#endif
}

static void slsi_purge_nan_discovery_info(struct netdev_vif *ndev_vif)
{
	struct slsi_nan_discovery_info *discoveryinfo;
	struct slsi_nan_discovery_info *prev = NULL;

	discoveryinfo = ndev_vif->nan.disc_info;
	while (discoveryinfo) {
		prev = discoveryinfo;
		discoveryinfo = discoveryinfo->next;
		kfree(prev);
	}
}

static void slsi_add_nan_discovery_info(struct netdev_vif *ndev_vif, u8 *addr, u16 publish_subscribe_id, u32 req_instance_id)
{
	struct slsi_nan_discovery_info *discoveryinfo;
	struct slsi_nan_discovery_info *head = ndev_vif->nan.disc_info;

	discoveryinfo = kzalloc(sizeof(*discoveryinfo), GFP_KERNEL);
	if (!discoveryinfo)
		return;
	ether_addr_copy(discoveryinfo->peer_addr, addr);
	discoveryinfo->session_id = publish_subscribe_id;
	discoveryinfo->match_id = req_instance_id;
	discoveryinfo->next = head;
	ndev_vif->nan.disc_info = discoveryinfo;
}

static int slsi_vendor_nan_command_reply(struct wiphy *wiphy, u32 status, u32 error, u32 response_type,
					 u16 id, struct slsi_hal_nan_capabilities *capabilities, u16 req_id,
					 struct netdev_vif *ndev_vif)
{
	struct sk_buff  *reply;
	u8              mac_addr[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int             ret = 0;
#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
#endif

	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, NLMSG_DEFAULT_SIZE);
	if (!reply) {
		SLSI_WARN_NODEV("SKB alloc failed for vendor_cmd reply, error %d to ENOMEM\n", error);
		return -ENOMEM;
	}

	ret |= nla_put_u32(reply, NAN_REPLY_ATTR_STATUS_TYPE, status);
	ret |= nla_put_u32(reply, NAN_REPLY_ATTR_VALUE, error);
	ret |= nla_put_u32(reply, NAN_REPLY_ATTR_RESPONSE_TYPE, response_type);
	ret |= nla_put_u16(reply, NAN_REPLY_ATTR_HAL_TRANSACTION_ID, req_id);
	if (response_type == NAN_RESPONSE_ENABLED) {
		if (ndev_vif)
			ret |= nla_put(reply, NAN_EVT_ATTR_DISCOVERY_ENGINE_MAC_ADDR, ETH_ALEN, ndev_vif->nan.local_nmi);
		else
			ret |= nla_put(reply, NAN_EVT_ATTR_DISCOVERY_ENGINE_MAC_ADDR, ETH_ALEN, mac_addr);
	}
	if (capabilities) {
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_CONCURRENT_CLUSTER,
			    capabilities->max_concurrent_nan_clusters);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_PUBLISHES, capabilities->max_publishes);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SUBSCRIBES, capabilities->max_subscribes);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SERVICE_NAME_LEN, capabilities->max_service_name_len);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_MATCH_FILTER_LEN, capabilities->max_match_filter_len);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_TOTAL_MATCH_FILTER_LEN,
			    capabilities->max_total_match_filter_len);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SERVICE_SPECIFIC_INFO_LEN,
			    capabilities->max_service_specific_info_len);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_VSA_DATA_LEN, capabilities->max_vsa_data_len);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_MESH_DATA_LEN, capabilities->max_mesh_data_len);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_NDI_INTERFACES, capabilities->max_ndi_interfaces);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_NDP_SESSIONS, capabilities->max_ndp_sessions);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_APP_INFO_LEN, capabilities->max_app_info_len);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_QUEUED_TRANSMIT_FOLLOWUP_MGS,
			    capabilities->max_queued_transmit_followup_msgs);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SUBSCRIBE_ADDRESS,
			    capabilities->max_subscribe_address);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_CIPHER_SUITES_SUPPORTED,
			    capabilities->cipher_suites_supported);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SDEA_SERVICE_SPECIFIC_INFO_LEN,
				   capabilities->max_sdea_service_specific_info_len);
		ret |= nla_put_u32(reply, NAN_REPLY_ATTR_CAP_INSTANT_COMM_SUPPORTED,
				   capabilities->instant_comm_supported);
	} else if (id) {
		if (response_type < NAN_DP_INTERFACE_CREATE)
			ret |= nla_put_u16(reply, NAN_REPLY_ATTR_PUBLISH_SUBSCRIBE_TYPE, id);
#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
		else if (response_type == NAN_BOOTSTRAPPING_INITIATOR_RESPONSE ||
			 response_type == NAN_BOOTSTRAPPING_RESPONDER_RESPONSE)
			ret |= nla_put_u32(reply, NAN_REPLY_ATTR_BOOTSTRAPPING_INSTANCE_ID, id);
		else if (response_type == NAN_PAIRING_INITIATOR_RESPONSE ||
			 response_type == NAN_PAIRING_RESPONDER_RESPONSE) {
			ret |= nla_put_u32(reply, NAN_REPLY_ATTR_PAIRING_INSTANCE_ID, id);
			ether_addr_copy(sdev->pasn_params.own_disc_mac_addr, ndev_vif->nan.local_nmi);
			ether_addr_copy(sdev->pasn_params.bssid_mac_addr, sdev->nan_cluster_id);
			ret |= nla_put(reply, NAN_REPLY_ATTR_PAIRING_OWN_DISC_MAC_ADDR, ETH_ALEN,
				       sdev->pasn_params.own_disc_mac_addr);
			ret |= nla_put(reply, NAN_REPLY_ATTR_PAIRING_BSSID_MAC_ADDR, ETH_ALEN,
				       sdev->pasn_params.bssid_mac_addr);
			ret |= nla_put_u16(reply, NAN_REPLY_ATTR_PAIRING_PUBLISH_ID,
					   sdev->pasn_params.publish_id);
			ret |= nla_put_u16(reply, NAN_REPLY_ATTR_PAIRING_DIALOG_TOKEN,
					   sdev->pasn_params.dialog_token);
			ret |= nla_put_u16(reply, NAN_REPLY_ATTR_PAIRING_BOOTSTRAPPING_METHOD,
					   sdev->pasn_params.bootstrapping_method);
			ret |= nla_put(reply, NAN_REPLY_ATTR_PAIRING_NIRA_LOCAL_NONCE, NAN_IDENTITY_NONCE_LEN,
					   sdev->pasn_params.local_nonce);
			ret |= nla_put(reply, NAN_REPLY_ATTR_PAIRING_NIRA_LOCAL_TAG, NAN_IDENTITY_TAG_LEN,
					   sdev->pasn_params.local_tag);
			if (response_type == NAN_PAIRING_RESPONDER_RESPONSE) {
				ether_addr_copy(sdev->pasn_params.peer_disc_mac_addr,
						ndev_vif->nan.pairing_response_req.peer_disc_mac_addr);
				ret |= nla_put(reply, NAN_REPLY_ATTR_PAIRING_PEER_DISC_MAC_ADDR, ETH_ALEN,
					       sdev->pasn_params.peer_disc_mac_addr);
			}
		} else if (response_type == NAN_PAIRING_END)
			ret |= nla_put_u32(reply, NAN_REPLY_ATTR_PAIRING_INSTANCE_ID, id);
#endif
		else
			ret |= nla_put_u16(reply, NAN_REPLY_ATTR_NDP_INSTANCE_ID, id);
	}
	if (ret) {
		SLSI_ERR_NODEV("Error in nla_put:0x%x\n", ret);
		kfree_skb(reply);
	} else {
		ret = cfg80211_vendor_cmd_reply(reply);
		if (ret)
			SLSI_ERR_NODEV("FAILED to reply nan coammnd. response_type:%d, error %d to %d\n",
				       response_type, error, ret);
		else
			ret = error;
	}

	return ret;
}

static int slsi_nan_get_sdea_params_nl(struct slsi_dev *sdev, struct slsi_nan_sdea_ctrl_params *sdea_params,
				       const struct nlattr *iter, int nl_attr_id)
{
	switch (nl_attr_id) {
	case NAN_REQ_ATTR_SDEA_PARAM_NDP_TYPE:
		slsi_util_nla_get_u8(iter, &sdea_params->ndp_type);
		sdea_params->config_nan_data_path = 1;
		break;
	case NAN_REQ_ATTR_SDEA_PARAM_SECURITY_CFG:
		slsi_util_nla_get_u8(iter, &sdea_params->security_cfg);
		sdea_params->config_nan_data_path = 1;
		break;
	case NAN_REQ_ATTR_SDEA_PARAM_RANGING_STATE:
		slsi_util_nla_get_u8(iter, &sdea_params->ranging_state);
		break;
	case NAN_REQ_ATTR_SDEA_PARAM_RANGE_REPORT:
		slsi_util_nla_get_u8(iter, &sdea_params->range_report);
		break;
	case NAN_REQ_ATTR_SDEA_PARAM_QOS_CFG:
		slsi_util_nla_get_u8(iter, &sdea_params->qos_cfg);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int slsi_nan_get_ranging_cfg_nl(struct slsi_dev *sdev, struct slsi_nan_ranging_cfg *ranging_cfg,
				       const struct nlattr *iter, int nl_attr_id)
{
	switch (nl_attr_id) {
	case NAN_REQ_ATTR_RANGING_CFG_INTERVAL:
		slsi_util_nla_get_u32(iter, &ranging_cfg->ranging_interval_msec);
		break;
	case NAN_REQ_ATTR_RANGING_CFG_INDICATION:
		slsi_util_nla_get_u32(iter, &ranging_cfg->config_ranging_indications);
		break;
	case NAN_REQ_ATTR_RANGING_CFG_INGRESS_MM:
		slsi_util_nla_get_u32(iter, &ranging_cfg->distance_ingress_mm);
		break;
	case NAN_REQ_ATTR_RANGING_CFG_EGRESS_MM:
		slsi_util_nla_get_u32(iter, &ranging_cfg->distance_egress_mm);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int slsi_nan_get_security_info_nl(struct slsi_dev *sdev, struct slsi_nan_security_info *sec_info,
					 const struct nlattr *iter, int nl_attr_id)
{
	u32 key_type;

	switch (nl_attr_id) {
	case NAN_REQ_ATTR_CIPHER_TYPE:
		slsi_util_nla_get_u32(iter, &sec_info->cipher_type);
		break;
	case NAN_REQ_ATTR_SECURITY_KEY_TYPE:
		slsi_util_nla_get_u32(iter, &key_type);
		sec_info->key_info.key_type = key_type;
		break;
	case NAN_REQ_ATTR_SECURITY_PMK_LEN:
		slsi_util_nla_get_u32(iter, &sec_info->key_info.body.pmk_info.pmk_len);
		break;
	case NAN_REQ_ATTR_SECURITY_PMK:
		if (sec_info->key_info.body.pmk_info.pmk_len > SLSI_NAN_PMK_INFO_LEN)
			return -EINVAL;
		slsi_util_nla_get_data(iter, sec_info->key_info.body.pmk_info.pmk_len,
				       sec_info->key_info.body.pmk_info.pmk);
		break;
	case NAN_REQ_ATTR_SECURITY_PASSPHRASE_LEN:
		slsi_util_nla_get_u32(iter, &sec_info->key_info.body.passphrase_info.passphrase_len);
		break;
	case NAN_REQ_ATTR_SECURITY_PASSPHRASE:
		if (sec_info->key_info.body.passphrase_info.passphrase_len > SLSI_NAN_SECURITY_MAX_PASSPHRASE_LEN)
			return -EINVAL;
		slsi_util_nla_get_data(iter, sec_info->key_info.body.passphrase_info.passphrase_len,
				       sec_info->key_info.body.passphrase_info.passphrase);
		break;
	case NAN_REQ_ATTR_SCID_LEN:
		slsi_util_nla_get_u32(iter, &sec_info->scid_len);
		break;
	case NAN_REQ_ATTR_SCID:
		if (sec_info->scid_len > sizeof(sec_info->scid))
			sec_info->scid_len = sizeof(sec_info->scid);
		slsi_util_nla_get_data(iter, sec_info->scid_len, sec_info->scid);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int slsi_nan_get_range_resp_cfg_nl(struct slsi_dev *sdev, struct slsi_nan_range_response_cfg *cfg,
					  const struct nlattr *iter, int nl_attr_id)
{
	u16 range_response;

	switch (nl_attr_id) {
	case NAN_REQ_ATTR_RANGE_RESPONSE_CFG_PUBLISH_ID:
		slsi_util_nla_get_u16(iter, &cfg->publish_id);
		break;

	case NAN_REQ_ATTR_RANGE_RESPONSE_CFG_REQUESTOR_ID:
		slsi_util_nla_get_u32(iter, &cfg->requestor_instance_id);
		break;

	case NAN_REQ_ATTR_RANGE_RESPONSE_CFG_PEER_ADDR:
		slsi_util_nla_get_data(iter, ETH_ALEN, cfg->peer_addr);
		break;

	case NAN_REQ_ATTR_RANGE_RESPONSE_CFG_RANGING_RESPONSE:
		slsi_util_nla_get_u16(iter, &range_response);
		cfg->ranging_response = range_response;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
static int slsi_nan_get_pairing_cfg_nl(struct slsi_dev *sdev, struct slsi_nan_pairing_config *cfg,
				       const struct nlattr *iter, int nl_attr_id)
{
	switch (nl_attr_id) {
	case NAN_REQ_ATTR_PAIRING_CFG_ENABLE_PAIRING_SETUP:
		slsi_util_nla_get_u32(iter, &cfg->enable_pairing_setup);
		break;

	case NAN_REQ_ATTR_PAIRING_CFG_ENABLE_PAIRING_CACHE:
		slsi_util_nla_get_u32(iter, &cfg->enable_pairing_cache);
		break;

	case NAN_REQ_ATTR_PAIRING_CFG_ENABLE_PAIRING_VERIFICATION:
		slsi_util_nla_get_u32(iter, &cfg->enable_pairing_verification);
		break;

	case NAN_REQ_ATTR_PAIRING_CFG_SUPPORTED_BOOTSTRAPPING_METHODS:
		slsi_util_nla_get_u16(iter, &cfg->supported_bootstrapping_methods);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}
#endif

/* NAN HAL REQUESTS */

static int slsi_nan_enable_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_enable_req *hal_req,
					 const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;
	u8 val = 0;
	u32 random_interval = 0, channel_5g_val = 0, instant_mode = 0, instant_mode_channel = 0;
#ifdef SCSC_SEP_VERSION
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
#endif

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_MASTER_PREF:
			slsi_util_nla_get_u8(iter, &hal_req->master_pref);
			break;

		case NAN_REQ_ATTR_CLUSTER_LOW:
			slsi_util_nla_get_u16(iter, &hal_req->cluster_low);
			break;

		case NAN_REQ_ATTR_CLUSTER_HIGH:
			slsi_util_nla_get_u16(iter, &hal_req->cluster_high);
			break;

		case NAN_REQ_ATTR_SUPPORT_5G_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->support_5g_val);
			hal_req->config_support_5g = 1;
			break;

		case NAN_REQ_ATTR_SID_BEACON_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->sid_beacon_val);
			hal_req->config_sid_beacon = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_2G4_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_close_2dot4g_val);
			hal_req->config_2dot4g_rssi_close = 1;
			break;

		case NAN_REQ_ATTR_RSSI_MIDDLE_2G4_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_middle_2dot4g_val);
			hal_req->config_2dot4g_rssi_middle = 1;
			break;

		case NAN_REQ_ATTR_RSSI_PROXIMITY_2G4_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_proximity_2dot4g_val);
			hal_req->config_2dot4g_rssi_proximity = 1;
			break;

		case NAN_REQ_ATTR_HOP_COUNT_LIMIT_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->hop_count_limit_val);
			hal_req->config_hop_count_limit = 1;
			break;

		case NAN_REQ_ATTR_SUPPORT_2G4_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->support_2dot4g_val);
			hal_req->config_2dot4g_support = 1;
			break;

		case NAN_REQ_ATTR_BEACONS_2G4_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->beacon_2dot4g_val);
			hal_req->config_2dot4g_beacons = 1;
			break;

		case NAN_REQ_ATTR_SDF_2G4_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->sdf_2dot4g_val);
			hal_req->config_2dot4g_sdf = 1;
			break;

		case NAN_REQ_ATTR_BEACON_5G_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->beacon_5g_val);
			hal_req->config_5g_beacons = 1;
			break;

		case NAN_REQ_ATTR_SDF_5G_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->sdf_5g_val);
			hal_req->config_5g_sdf = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_5G_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_close_5g_val);
			hal_req->config_5g_rssi_close = 1;
			break;

		case NAN_REQ_ATTR_RSSI_MIDDLE_5G_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_middle_5g_val);
			hal_req->config_5g_rssi_middle = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_PROXIMITY_5G_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_close_proximity_5g_val);
			hal_req->config_5g_rssi_close_proximity = 1;
			break;

		case NAN_REQ_ATTR_RSSI_WINDOW_SIZE_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_window_size_val);
			hal_req->config_rssi_window_size = 1;
			break;

		case NAN_REQ_ATTR_OUI_VAL:
			slsi_util_nla_get_u32(iter, &hal_req->oui_val);
			hal_req->config_oui = 1;
			break;

		case NAN_REQ_ATTR_MAC_ADDR_VAL:
			slsi_util_nla_get_data(iter, ETH_ALEN, hal_req->intf_addr_val);
			hal_req->config_intf_addr = 1;
			break;

		case NAN_REQ_ATTR_CLUSTER_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->config_cluster_attribute_val);
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_DWELL_TIME:
			slsi_util_nla_get_data(iter, sizeof(hal_req->scan_params_val.dwell_time),
					       hal_req->scan_params_val.dwell_time);
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_PERIOD:
			slsi_util_nla_get_data(iter, sizeof(hal_req->scan_params_val.scan_period),
					       hal_req->scan_params_val.scan_period);
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_RANDOM_FACTOR_FORCE_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->random_factor_force_val);
			hal_req->config_random_factor_force = 1;
			break;

		case NAN_REQ_ATTR_HOP_COUNT_FORCE_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->hop_count_force_val);
			hal_req->config_hop_count_force = 1;
			break;

		case NAN_REQ_ATTR_CHANNEL_2G4_MHZ_VAL:
			slsi_util_nla_get_u32(iter, &hal_req->channel_24g_val);
			hal_req->config_24g_channel = 1;
			break;

		case NAN_REQ_ATTR_CHANNEL_5G_MHZ_VAL:
			slsi_util_nla_get_u32(iter, &channel_5g_val);
			hal_req->channel_5g_val = (int)channel_5g_val;
			hal_req->config_5g_channel = 1;
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SID_BEACON_VAL:
			slsi_util_nla_get_u8(iter, &val);
			hal_req->subscribe_sid_beacon_val = (u32)val;
			hal_req->config_subscribe_sid_beacon = 1;
			break;

		case NAN_REQ_ATTR_DW_2G4_INTERVAL:
			slsi_util_nla_get_u8(iter, &val);
			hal_req->dw_2dot4g_interval_val = (u32)val;
			/* valid range for 2.4G is 1-5 */
			if (hal_req->dw_2dot4g_interval_val > 0  && hal_req->dw_2dot4g_interval_val < 5)
				hal_req->config_2dot4g_dw_band = 1;
			break;

		case NAN_REQ_ATTR_DW_5G_INTERVAL:
			slsi_util_nla_get_u8(iter, &val);
			hal_req->dw_5g_interval_val = (u32)val;
			/* valid range for 5g is 0-5 */
			if (hal_req->dw_5g_interval_val < 5)
				hal_req->config_5g_dw_band = 1;
			break;

		case NAN_REQ_ATTR_DISC_MAC_ADDR_RANDOM_INTERVAL:
			slsi_util_nla_get_u32(iter, &random_interval);
			if (slsi_get_nan_mac_random())
				hal_req->disc_mac_addr_rand_interval_sec = random_interval;
			else
				hal_req->disc_mac_addr_rand_interval_sec = 0;
#ifdef SCSC_SEP_VERSION
			if (random_interval & BIT(31)) {
				hal_req->disc_mac_addr_rand_interval_sec &= SLSI_NAN_MAC_RANDOM_INTERVAL_MASK;
				ndev_vif->nan.disable_cluster_merge = (random_interval & BIT(30)) ? 0 : 1;
				slsi_set_mib_fast_connect_enabled(sdev, (random_interval & BIT(29)) ? 1 : 0);
			}
#endif
			break;
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;
		case NAN_REQ_ATTR_DISCOVERY_BEACON_INT:
			slsi_util_nla_get_u32(iter, &hal_req->discovery_beacon_interval_ms);
			break;
		case NAN_REQ_ATTR_NSS:
			slsi_util_nla_get_u32(iter, &hal_req->nss_discovery);
			break;
		case NAN_REQ_ATTR_ENABLE_RANGING:
			slsi_util_nla_get_u32(iter, &hal_req->enable_ranging);
			break;
		case NAN_REQ_ATTR_DW_EARLY_TERMINATION:
			slsi_util_nla_get_u32(iter, &hal_req->enable_dw_early_termination);
			break;
		case NAN_REQ_ATTR_ENABLE_INSTANT_MODE:
			slsi_util_nla_get_u32(iter, &instant_mode);
			hal_req->enable_instant_mode = (u16)instant_mode;
			break;
		case NAN_REQ_ATTR_INSTANT_MODE_CHANNEL:
			slsi_util_nla_get_u32(iter, &instant_mode_channel);
			hal_req->instant_mode_channel = (u16)instant_mode_channel;
			break;
		default:
			SLSI_INFO(sdev, "Unexpected NAN enable attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_enable(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_hal_nan_enable_req hal_req;
	int ret,  r;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	u8 nan_vif_mac_address[ETH_ALEN];
	u8 broadcast_mac[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;
	struct slsi_hal_nan_vendor_prev_cmd_data_info *prev_cmd_data_info, *tmp;

	hal_req.transaction_id = 0;
	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	ndev_vif = netdev_priv(dev);

	reply_status = slsi_nan_enable_get_nl_params(sdev, &hal_req, data, len);
	if (reply_status != SLSI_HAL_NAN_STATUS_SUCCESS) {
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (ndev_vif->activated) {
		ret = -EINVAL;
		SLSI_DBG1(sdev, SLSI_GSCAN, "Already Enabled. Req Rejected\n");
		goto exit_with_mutex;
	}
	ndev_vif->vif_type = FAPI_VIFTYPE_NAN;
	if (slsi_get_nan_mac_random())
		slsi_net_randomize_nmi_ndi(sdev);

	if (hal_req.config_intf_addr)
		ether_addr_copy(nan_vif_mac_address, hal_req.intf_addr_val);
	else
		slsi_nan_get_mac(sdev, nan_vif_mac_address);

	ret = slsi_mlme_add_vif(sdev, dev, nan_vif_mac_address, broadcast_mac);
	if (ret) {
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		SLSI_ERR(sdev, "failed to add nan vif. Cannot start NAN\n");
	} else {
		ndev_vif->nan.nan_enable_status = 0;
		if (slsi_vif_activated(sdev, dev) != 0)
			SLSI_NET_ERR(dev, "slsi_vif_activated failed\n");
		ret = slsi_mlme_nan_enable(sdev, dev, &hal_req);
		if (ret) {
			SLSI_ERR(sdev, "failed to enable NAN.\n");
			if (ndev_vif->nan.nan_enable_status == FAPI_RESULTCODE_INVALID_PARAMETERS)
				reply_status = SLSI_HAL_NAN_STATUS_INVALID_PARAM;
			else
				reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;

			if (slsi_mlme_del_vif(sdev, dev) != 0)
				SLSI_NET_ERR(dev, "slsi_mlme_del_vif failed\n");
			ndev_vif->activated = false;
			memset(ndev_vif->nan.service_id_map, 0,
			       BITS_TO_LONGS(SLSI_NAN_MAX_SERVICE_ID) * sizeof(unsigned long));
		} else {
			ndev_vif->nan.master_pref_value = hal_req.master_pref;
			ether_addr_copy(ndev_vif->nan.local_nmi, nan_vif_mac_address);
			ndev_vif->nan.state = 1;
			if (hal_req.config_24g_channel)
				ndev_vif->nan.operating_channel[0] = hal_req.channel_24g_val;
			if (hal_req.config_5g_channel)
				ndev_vif->nan.operating_channel[1] = hal_req.channel_5g_val;
			if (hal_req.config_hop_count_limit)
				ndev_vif->nan.hopcount = hal_req.hop_count_limit_val;
			slsi_eth_zero_addr(sdev->nan_cluster_id);
			ndev_vif->nan.random_mac_interval_sec = hal_req.disc_mac_addr_rand_interval_sec;
			SLSI_INFO(sdev,
				  "trans_id:%d master_pref:%d 2gChan:%d 5gChan:%d mac_random_interval:%d\n",
				  hal_req.transaction_id, hal_req.master_pref, hal_req.channel_24g_val,
				  hal_req.channel_5g_val, hal_req.disc_mac_addr_rand_interval_sec);
		}
	}
	ether_addr_copy(ndev_vif->nan.local_nmi, nan_vif_mac_address);
	init_completion(&ndev_vif->nan.ndp_delay);
exit_with_mutex:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_ENABLED, 0, NULL, hal_req.transaction_id, ndev_vif);
	if (reply_status == SLSI_HAL_NAN_STATUS_SUCCESS && ret == WIFI_HAL_SUCCESS) {
		/* Send STD+ command in queue to firmware */
		list_for_each_entry_safe(prev_cmd_data_info, tmp, &sdev->slsi_hal_nan_vendor_prev_cmd_data, list) {
			r = slsi_vendor_nan_set_command(wiphy, wdev, prev_cmd_data_info, sizeof(*prev_cmd_data_info));
			list_del(&prev_cmd_data_info->list);
			kfree(prev_cmd_data_info);
			if (!r)
				SLSI_INFO(sdev, "[STD+][DRIVER] success to prev_cmd_data : %d\n", r);
			else
				SLSI_ERR(sdev, "[STD+][DRIVER] fail to prev_cmd_data : %d\n", r);
		}
	}
	return 0;
}

int slsi_nan_disable(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct net_device *data_dev;
	struct netdev_vif *ndev_vif = NULL, *data_ndev_vif;
	u8 i,j;
	int type, tmp;
	const struct nlattr *iter;
	u16 transaction_id = 0;
	struct slsi_peer *peer;

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &transaction_id);
			break;
		default:
			break;
		}
	}

	if (dev) {
		ndev_vif = netdev_priv(dev);
		SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
		SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

		if (ndev_vif->activated) {
			if (slsi_mlme_del_vif(sdev, dev) != 0)
				SLSI_NET_ERR(dev, "slsi_mlme_del_vif failed\n");
			ndev_vif->activated = false;
			SLSI_INFO(sdev, "transaction_id:%d\n", transaction_id);
		} else {
			SLSI_INFO(sdev, "transaction_id:%d NAN not active\n", transaction_id);
		}
		for (i = SLSI_NAN_DATA_IFINDEX_START; i < CONFIG_SCSC_WLAN_MAX_INTERFACES + 1; i++) {
			data_dev = slsi_get_netdev_locked(sdev, i);
			if (data_dev) {
				data_ndev_vif = netdev_priv(data_dev);
				SLSI_MUTEX_LOCK(data_ndev_vif->vif_mutex);
				slsi_vif_cleanup(sdev, data_dev, true, 0);
				slsi_spinlock_lock(&ndev_vif->peer_lock);
				for (j = 0; j < SLSI_ADHOC_PEER_CONNECTIONS_MAX; j++) {
					peer = data_ndev_vif->peer_sta_record[j];
					if (peer && peer->valid) {
						slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_DISCONNECTED);
						peer->ndp_count = 0;
						slsi_peer_remove(sdev, dev, peer);
					}
				}
				slsi_spinlock_unlock(&ndev_vif->peer_lock);
				data_ndev_vif->nan.ndp_count = 0;
				SLSI_MUTEX_UNLOCK(data_ndev_vif->vif_mutex);
			}
		}
		slsi_purge_nan_discovery_info(ndev_vif);
		memset(&ndev_vif->nan, 0, sizeof(ndev_vif->nan));
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
		SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
	} else {
		SLSI_WARN(sdev, "No NAN interface!!");
	}

	slsi_vendor_nan_command_reply(wiphy, SLSI_HAL_NAN_STATUS_SUCCESS, 0, NAN_RESPONSE_DISABLED, 0, NULL, transaction_id, ndev_vif);

	return 0;
}

static int slsi_nan_publish_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_publish_req *hal_req,
					  const void *data, int len)
{
	int type, tmp, r;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_PUBLISH_ID:
			slsi_util_nla_get_u16(iter, &hal_req->publish_id);
			break;

		case NAN_REQ_ATTR_PUBLISH_TTL:
			slsi_util_nla_get_u16(iter, &hal_req->ttl);
			break;

		case NAN_REQ_ATTR_PUBLISH_PERIOD:
			slsi_util_nla_get_u16(iter, &hal_req->period);
			break;

		case NAN_REQ_ATTR_PUBLISH_TYPE:
			slsi_util_nla_get_u8(iter, &hal_req->publish_type);
			break;

		case NAN_REQ_ATTR_PUBLISH_TX_TYPE:
			slsi_util_nla_get_u8(iter, &hal_req->tx_type);
			break;

		case NAN_REQ_ATTR_PUBLISH_COUNT:
			slsi_util_nla_get_u8(iter, &hal_req->publish_count);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_NAME_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->service_name_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_NAME:
			if (hal_req->service_name_len > SLSI_HAL_NAN_MAX_SERVICE_NAME_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->service_name_len, hal_req->service_name);
			break;

		case NAN_REQ_ATTR_PUBLISH_MATCH_ALGO:
			slsi_util_nla_get_u8(iter, &hal_req->publish_match_indicator);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_INFO_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_INFO:
			if (hal_req->service_specific_info_len > SLSI_HAL_NAN_MAX_SERVICE_SPECIFIC_INFO_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->service_specific_info_len,
					       hal_req->service_specific_info);
			break;

		case NAN_REQ_ATTR_PUBLISH_RX_MATCH_FILTER_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->rx_match_filter_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_RX_MATCH_FILTER:
			if (hal_req->rx_match_filter_len > SLSI_HAL_NAN_MAX_MATCH_FILTER_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter,  hal_req->rx_match_filter_len, hal_req->rx_match_filter);
			break;

		case NAN_REQ_ATTR_PUBLISH_TX_MATCH_FILTER_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->tx_match_filter_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_TX_MATCH_FILTER:
			if (hal_req->tx_match_filter_len > SLSI_HAL_NAN_MAX_MATCH_FILTER_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->tx_match_filter_len, hal_req->tx_match_filter);
			break;

		case NAN_REQ_ATTR_PUBLISH_RSSI_THRESHOLD_FLAG:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_threshold_flag);
			break;

		case NAN_REQ_ATTR_PUBLISH_CONN_MAP:
			slsi_util_nla_get_u8(iter, &hal_req->connmap);
			break;

		case NAN_REQ_ATTR_PUBLISH_RECV_IND_CFG:
			slsi_util_nla_get_u8(iter, &hal_req->recv_indication_cfg);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->sdea_service_specific_info_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA:
			if (hal_req->sdea_service_specific_info_len > SLSI_HAL_NAN_MAX_SDEA_SERVICE_SPEC_INFO_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->sdea_service_specific_info_len,
					       hal_req->sdea_service_specific_info);
			break;

		case NAN_REQ_ATTR_RANGING_AUTO_RESPONSE:
			slsi_util_nla_get_u8(iter, &hal_req->ranging_auto_response);
			break;

		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;
#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
		case NAN_REQ_ATTR_NIK:
			slsi_util_nla_get_data(iter, SLSI_HAL_NAN_IDENTITY_KEY_LEN, hal_req->nan_identity_key);
			break;
#endif

		default:
			r = slsi_nan_get_sdea_params_nl(sdev, &hal_req->sdea_params, iter, type);
			if (r)
				r = slsi_nan_get_ranging_cfg_nl(sdev, &hal_req->ranging_cfg, iter, type);
			if (r)
				r = slsi_nan_get_security_info_nl(sdev, &hal_req->sec_info, iter, type);
			if (r)
				r = slsi_nan_get_range_resp_cfg_nl(sdev, &hal_req->range_response_cfg, iter, type);
#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
			if (r)
				r = slsi_nan_get_pairing_cfg_nl(sdev, &hal_req->pairing_cfg, iter, type);
#endif
			if (r)
				SLSI_INFO(sdev, "Unexpected NAN publish attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_publish(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_hal_nan_publish_req *hal_req;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	int ret, i = 0;
	u32 reply_status;
	u32 publish_id = 0;
	u16 transaction_id = 0;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "failed to alloc hal_req\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_publish_get_nl_params(sdev, hal_req, data, len);
	transaction_id = hal_req->transaction_id;
	if (reply_status != SLSI_HAL_NAN_STATUS_SUCCESS) {
		kfree(hal_req);
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req->publish_id) {
		hal_req->publish_id = slsi_nan_get_new_publish_subscribe_id(ndev_vif);
	} else if (!slsi_nan_is_publish_subscribe_id_active(ndev_vif, hal_req->publish_id)) {
		SLSI_WARN(sdev, "Publish id %d not found.\n", hal_req->publish_id);
		for (i = 0; i < BITS_TO_LONGS(SLSI_NAN_MAX_SERVICE_ID); i++)
			SLSI_WARN(sdev, "Service Id_Maps[%d]: 0x%x\n", i,
				  ndev_vif->nan.service_id_map[i]);
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		ret = -EINVAL;
		goto exit_with_lock;
	}

#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
	ndev_vif->nan.nan_enable_pairing_cache = hal_req->pairing_cfg.enable_pairing_cache;
	ndev_vif->nan.service_specific_info_len = hal_req->service_specific_info_len;
	memcpy(ndev_vif->nan.service_specific_info, hal_req->service_specific_info, hal_req->service_specific_info_len);
#endif

	if (hal_req->publish_id) {
		ret = slsi_mlme_nan_publish(sdev, dev, hal_req, hal_req->publish_id);
		if (ret) {
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
			SLSI_INFO(sdev, "transId:%d, pubId:%d failed\n",
				  hal_req->transaction_id, hal_req->publish_id);
		} else {
			publish_id = hal_req->publish_id;
			SLSI_INFO(sdev,
				  "transId:%d, pubId:%d type:%d recIndCfg:0x%x name:%s\n",
				  hal_req->transaction_id, hal_req->publish_id, hal_req->publish_type,
				  hal_req->recv_indication_cfg, hal_req->service_name);
			if (hal_req->service_specific_info_len)
				SLSI_INFO_HEX(sdev, hal_req->service_specific_info, hal_req->service_specific_info_len,
					      "service_specific_info\n");
		}
	} else {
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		SLSI_WARN(sdev, "Too Many PUBLISH REQ\n");
		for (i = 0; i < BITS_TO_LONGS(SLSI_NAN_MAX_SERVICE_ID); i++)
			SLSI_WARN(sdev, "Service Id_Maps[%d]: 0x%x\n", i,
				  ndev_vif->nan.service_id_map[i]);
		ret = -ENOTSUPP;
	}
exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(hal_req);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_PUBLISH, publish_id, NULL, transaction_id, ndev_vif);
	return ret;
}

int slsi_nan_publish_cancel(struct wiphy *wiphy, struct wireless_dev *wdev,
			    const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	int type, tmp, ret = 0, i = 0;
	u16 publish_id = 0, transaction_id = 0;
	const struct nlattr *iter;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	ndev_vif = netdev_priv(dev);
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_PUBLISH_ID:
			slsi_util_nla_get_u16(iter, &(publish_id));
			break;
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &(transaction_id));
			break;

		default:
			SLSI_INFO(sdev, "Unexpected NAN publishcancel attribute TYPE:%d\n", type);
		}
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}
	if (!publish_id || !slsi_nan_is_publish_subscribe_id_active(ndev_vif, publish_id)) {
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		SLSI_WARN(sdev, "pubId(%d) not active.\n", publish_id);
		for (i = 0; i < BITS_TO_LONGS(SLSI_NAN_MAX_SERVICE_ID); i++)
			SLSI_WARN(sdev, "Service Id_Maps[%d]: 0x%x\n", i,
				  ndev_vif->nan.service_id_map[i]);
	} else {
		ret = slsi_mlme_nan_publish(sdev, dev, NULL, publish_id);
		if (ret) {
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
			SLSI_INFO(sdev, "transId:%d pubId:%d failed\n", transaction_id, publish_id);
		} else {
			SLSI_INFO(sdev, "transId:%d pubId:%d\n", transaction_id, publish_id);
		}
	}
exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	ret = slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_PUBLISH_CANCEL,
					    publish_id, NULL, transaction_id, ndev_vif);
	return ret;
}

static int slsi_nan_subscribe_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_subscribe_req *hal_req,
					    const void *data, int len)
{
	int type, tmp, r;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_SUBSCRIBE_ID:
			slsi_util_nla_get_u16(iter, &hal_req->subscribe_id);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TTL:
			slsi_util_nla_get_u16(iter, &hal_req->ttl);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_PERIOD:
			slsi_util_nla_get_u16(iter, &hal_req->period);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TYPE:
			slsi_util_nla_get_u8(iter, &hal_req->subscribe_type);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RESP_FILTER_TYPE:
			slsi_util_nla_get_u8(iter, &hal_req->service_response_filter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RESP_INCLUDE:
			slsi_util_nla_get_u8(iter, &hal_req->service_response_include);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_USE_RESP_FILTER:
			slsi_util_nla_get_u8(iter, &hal_req->use_service_response_filter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SSI_REQUIRED:
			slsi_util_nla_get_u8(iter, &hal_req->ssi_required_for_match_indication);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_MATCH_INDICATOR:
			slsi_util_nla_get_u8(iter, &hal_req->subscribe_match_indicator);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_COUNT:
			slsi_util_nla_get_u8(iter, &hal_req->subscribe_count);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_NAME_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->service_name_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_NAME:
			if (hal_req->service_name_len > SLSI_HAL_NAN_MAX_SERVICE_NAME_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->service_name_len, hal_req->service_name);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_INFO_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_INFO:
			if (hal_req->service_specific_info_len > SLSI_HAL_NAN_MAX_SERVICE_SPECIFIC_INFO_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->service_specific_info_len,
					       hal_req->service_specific_info);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RX_MATCH_FILTER_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->rx_match_filter_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RX_MATCH_FILTER:
			if (hal_req->rx_match_filter_len > SLSI_HAL_NAN_MAX_MATCH_FILTER_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->rx_match_filter_len, hal_req->rx_match_filter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TX_MATCH_FILTER_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->tx_match_filter_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TX_MATCH_FILTER:
			if (hal_req->tx_match_filter_len > SLSI_HAL_NAN_MAX_MATCH_FILTER_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->tx_match_filter_len, hal_req->tx_match_filter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RSSI_THRESHOLD_FLAG:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_threshold_flag);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_CONN_MAP:
			slsi_util_nla_get_u8(iter, &hal_req->connmap);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_NUM_INTF_ADDR_PRESENT:
			slsi_util_nla_get_u8(iter, &hal_req->num_intf_addr_present);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_INTF_ADDR:
			if (hal_req->num_intf_addr_present > SLSI_HAL_NAN_MAX_SUBSCRIBE_MAX_ADDRESS)
				return -EINVAL;
			slsi_util_nla_get_data(iter, (hal_req->num_intf_addr_present * ETH_ALEN), hal_req->intf_addr);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RECV_IND_CFG:
			slsi_util_nla_get_u8(iter, &hal_req->recv_indication_cfg);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->sdea_service_specific_info_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA:
			if (hal_req->sdea_service_specific_info_len > SLSI_HAL_NAN_MAX_SDEA_SERVICE_SPEC_INFO_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->sdea_service_specific_info_len,
					       hal_req->sdea_service_specific_info);

			break;

		case NAN_REQ_ATTR_RANGING_AUTO_RESPONSE:
			slsi_util_nla_get_u8(iter, &hal_req->ranging_auto_response);
			break;

		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;

#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
		case NAN_REQ_ATTR_NIK:
			slsi_util_nla_get_data(iter, SLSI_HAL_NAN_IDENTITY_KEY_LEN, hal_req->nan_identity_key);
			break;
#endif

		default:
			r = slsi_nan_get_sdea_params_nl(sdev, &hal_req->sdea_params, iter, type);
			if (r)
				r = slsi_nan_get_ranging_cfg_nl(sdev, &hal_req->ranging_cfg, iter, type);
			if (r)
				r = slsi_nan_get_security_info_nl(sdev, &hal_req->sec_info, iter, type);
			if (r)
				r = slsi_nan_get_range_resp_cfg_nl(sdev, &hal_req->range_response_cfg, iter, type);
#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
			if (r)
				r = slsi_nan_get_pairing_cfg_nl(sdev, &hal_req->pairing_cfg, iter, type);
#endif
			if (r)
				SLSI_INFO(sdev, "Unexpected NAN subscribe attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_subscribe(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	struct slsi_hal_nan_subscribe_req *hal_req;
	int ret, i = 0;
	u32 reply_status;
	u32 subscribe_id = 0;
	u16 transaction_id = 0;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS) {
		SLSI_ERR(sdev, " Pre Check Failed !!!\n");
		goto exit;
	}

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "Failed to alloc hal_req structure!!!\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_subscribe_get_nl_params(sdev, hal_req, data, len);
	transaction_id = hal_req->transaction_id;
	if (reply_status != SLSI_HAL_NAN_STATUS_SUCCESS) {
		kfree(hal_req);
		ret = -EINVAL;
		goto exit;
	}
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req->subscribe_id) {
		hal_req->subscribe_id = slsi_nan_get_new_publish_subscribe_id(ndev_vif);
	} else if (!slsi_nan_is_publish_subscribe_id_active(ndev_vif, hal_req->subscribe_id)) {
		SLSI_WARN(sdev, "subId %d not found.\n", hal_req->subscribe_id);
		for (i = 0; i < BITS_TO_LONGS(SLSI_NAN_MAX_SERVICE_ID); i++)
			SLSI_WARN(sdev, "Service Id_Maps[%d]: 0x%x\n", i,
				  ndev_vif->nan.service_id_map[i]);
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		ret = -EINVAL;
		goto exit_with_lock;
	}

#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
	ndev_vif->nan.nan_enable_pairing_cache = hal_req->pairing_cfg.enable_pairing_cache;
	ndev_vif->nan.service_specific_info_len = hal_req->service_specific_info_len;
	memcpy(ndev_vif->nan.service_specific_info, hal_req->service_specific_info, hal_req->service_specific_info_len);
#endif

	ret = slsi_mlme_nan_subscribe(sdev, dev, hal_req, hal_req->subscribe_id);
	if (ret) {
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		SLSI_INFO(sdev, "transId:%d subId:%d type:%d Failed\n",
			  hal_req->transaction_id, hal_req->subscribe_id, hal_req->subscribe_type);
	} else {
		SLSI_INFO(sdev, "transId:%d subId:%d type:%d\n name:%s", hal_req->transaction_id,
			  hal_req->subscribe_id, hal_req->subscribe_type, hal_req->service_name);
		if (hal_req->service_specific_info_len)
			SLSI_INFO_HEX(sdev, hal_req->service_specific_info, hal_req->service_specific_info_len,
				      "service_specific_info\n");
		subscribe_id = hal_req->subscribe_id;
	}

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(hal_req);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_SUBSCRIBE, subscribe_id, NULL,
				      transaction_id, ndev_vif);
	return ret;
}

int slsi_nan_subscribe_cancel(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	int type, tmp, ret = WIFI_HAL_ERROR_UNKNOWN, i = 0;
	u16 subscribe_id = 0, transaction_id = 0;
	const struct nlattr *iter;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	ndev_vif = netdev_priv(dev);

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_SUBSCRIBE_ID:
			slsi_util_nla_get_u16(iter, &(subscribe_id));
			break;
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &(transaction_id));
			break;
		default:
			SLSI_INFO(sdev, "Unexpected NAN subscribecancel attribute TYPE:%d\n", type);
		}
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (ndev_vif->activated) {
		if (!subscribe_id ||
		    !slsi_nan_is_publish_subscribe_id_active(ndev_vif, subscribe_id)) {
			SLSI_WARN(sdev, "subId(%d) not active.\n", subscribe_id);
			for (i = 0; i < BITS_TO_LONGS(SLSI_NAN_MAX_SERVICE_ID); i++)
				SLSI_WARN(sdev, "Service Id_Maps[%d]: 0x%x\n", i,
					  ndev_vif->nan.service_id_map[i]);
			reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		} else {
			ret = slsi_mlme_nan_subscribe(sdev, dev, NULL, subscribe_id);
			if (ret) {
				reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
				SLSI_INFO(sdev, "transId:%d subId:%d Failed\n", transaction_id, subscribe_id);
			} else {
				SLSI_INFO(sdev, "transId:%d subId:%d\n", transaction_id, subscribe_id);
			}
		}
	} else {
		SLSI_ERR(sdev, "vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	ret = slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_SUBSCRIBE_CANCEL,
					    subscribe_id, NULL, transaction_id, ndev_vif);
	return ret;
}

static int slsi_nan_followup_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_transmit_followup_req *hal_req,
					   const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_FOLLOWUP_ID:
			slsi_util_nla_get_u16(iter, &hal_req->publish_subscribe_id);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_REQUESTOR_ID:
			slsi_util_nla_get_u32(iter, &hal_req->requestor_instance_id);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_ADDR:
			slsi_util_nla_get_data(iter, ETH_ALEN, hal_req->addr);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_PRIORITY:
			slsi_util_nla_get_u8(iter, &hal_req->priority);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_TX_WINDOW:
			slsi_util_nla_get_u8(iter, &hal_req->dw_or_faw);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_SERVICE_NAME_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_SERVICE_NAME:
			if (hal_req->service_specific_info_len > SLSI_HAL_NAN_MAX_SDEA_SERVICE_SPEC_INFO_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->service_specific_info_len,
					       hal_req->service_specific_info);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_RECV_IND_CFG:
			slsi_util_nla_get_u8(iter, &hal_req->recv_indication_cfg);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->sdea_service_specific_info_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA:
			if (hal_req->sdea_service_specific_info_len > SLSI_HAL_NAN_MAX_SDEA_SERVICE_SPEC_INFO_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->sdea_service_specific_info_len,
					       hal_req->sdea_service_specific_info);
			break;

		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_SHARED_KEY_DESC_FLAG:
			slsi_util_nla_get_u8(iter, &hal_req->shared_key_desc_flag);
			break;

		default:
			SLSI_INFO(sdev, "Unexpected NAN followup attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_transmit_followup(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	struct slsi_hal_nan_transmit_followup_req hal_req;
	int ret = 0, i = 0;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	hal_req.transaction_id = 0;
	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_followup_get_nl_params(sdev, &hal_req, data, len);
	if (reply_status) {
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req.publish_subscribe_id ||
	    !slsi_nan_is_publish_subscribe_id_active(ndev_vif, hal_req.publish_subscribe_id)) {
		SLSI_WARN(sdev, "publish/Subscribe id %d not found.\n",
			  hal_req.publish_subscribe_id);
		for (i = 0; i < BITS_TO_LONGS(SLSI_NAN_MAX_SERVICE_ID); i++)
			SLSI_WARN(sdev, "Service Id_Maps[%d]: 0x%x\n", i,
				  ndev_vif->nan.service_id_map[i]);
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		ret = -EINVAL;
		goto exit_with_lock;
	}

	ret = slsi_mlme_nan_tx_followup(sdev, dev, &hal_req);

	if (ret) {
		if (ret == SLSI_HAL_NAN_STATUS_FOLLOWUP_QUEUE_FULL) {
			reply_status = ret;
			ret = 0;
		} else {
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
			ret = 0;
		}
	} else {
		SLSI_INFO(sdev,
			  "transId:%d serviceId:%d instanceId:%d\n",
			  hal_req.transaction_id, hal_req.publish_subscribe_id, hal_req.requestor_instance_id);
	}

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_TRANSMIT_FOLLOWUP, 0, NULL, hal_req.transaction_id, ndev_vif);
	return ret;
}

#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
static int slsi_nan_bootstrapping_request_get_nl_params(struct slsi_dev *sdev,
							struct slsi_hal_nan_bootstrapping_req *hal_req,
							const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_REQUESTOR_INSTANCE_ID:
			slsi_util_nla_get_u32(iter, &hal_req->requestor_instance_id);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_PEER_DISC_MAC_ADDR:
			slsi_util_nla_get_data(iter, ETH_ALEN, hal_req->addr);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_REQUEST_BOOTSTRAPPING_METHOD:
			slsi_util_nla_get_u16(iter, &hal_req->bootstrapping_method);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_SERVICE_SPECIFIC_INFO_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_SERVICE_SPECIFIC_INFO:
			slsi_util_nla_get_data(iter, hal_req->service_specific_info_len,
					       hal_req->service_specific_info);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_SDEA_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->sdea_service_specific_info_len);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_SDEA:
			slsi_util_nla_get_data(iter, hal_req->sdea_service_specific_info_len,
					       hal_req->sdea_service_specific_info);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_COOKIE_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->comebackinfo.cookie_length);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_COOKIE:
			slsi_util_nla_get_data(iter, hal_req->comebackinfo.cookie_length,
					       hal_req->comebackinfo.cookie);
			break;

		default:
			SLSI_INFO(sdev, "Unexpected NAN bootstrapping attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_bootstrapping_request(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	struct slsi_hal_nan_bootstrapping_req *hal_req;
	int ret = 0;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;
	u16 transaction_id = 0;
	u32 bootstrapping_instance_id = 0;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "failed to alloc hal_req\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_bootstrapping_request_get_nl_params(sdev, hal_req, data, len);
	transaction_id = hal_req->transaction_id;
	if (reply_status) {
		kfree(hal_req);
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	hal_req->bootstrapping_instance_id = slsi_nan_get_new_bootstrapping_instance_id(ndev_vif);
	if (hal_req->bootstrapping_instance_id) {
		ret = slsi_mlme_nan_bootstrapping_request(sdev, dev, hal_req);
		if (ret) {
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
			SLSI_INFO(sdev, "transId:%d, bootstrapping_instance_id:%d failed\n",
				  transaction_id, hal_req->bootstrapping_instance_id);
		} else {
			bootstrapping_instance_id = hal_req->bootstrapping_instance_id;
			SLSI_INFO(sdev,
				  "transId:%d, requestor_instance_id:%d, bootstrapping_instance_id:%d\n",
				  hal_req->transaction_id, hal_req->requestor_instance_id,
				  hal_req->bootstrapping_instance_id);
		}
	} else {
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_BOOTSTRAPPING_ID;
		ret = WIFI_HAL_ERROR_INVALID_REQUEST_ID;
	}

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(hal_req);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_BOOTSTRAPPING_INITIATOR_RESPONSE,
				      bootstrapping_instance_id, NULL, transaction_id, ndev_vif);
	return ret;
}

static int slsi_nan_bootstrapping_response_get_nl_params(struct slsi_dev *sdev,
							 struct slsi_hal_nan_bootstrapping_rsp *hal_req,
							 const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_SERVICE_INSTANCE_ID:
			slsi_util_nla_get_u16(iter, &hal_req->bootstrapping_instance_id);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_PEER_DISC_MAC_ADDR:
			slsi_util_nla_get_data(iter, ETH_ALEN, hal_req->addr);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_SERVICE_SPECIFIC_INFO_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_SERVICE_SPECIFIC_INFO:
			slsi_util_nla_get_data(iter, hal_req->service_specific_info_len,
					       hal_req->service_specific_info);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_SDEA_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->sdea_service_specific_info_len);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_SDEA:
			slsi_util_nla_get_data(iter, hal_req->sdea_service_specific_info_len,
					       hal_req->sdea_service_specific_info);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_COME_BACK_DELAY:
			slsi_util_nla_get_u32(iter, &hal_req->comebackinfo.comeback_delay);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_COOKIE_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->comebackinfo.cookie_length);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_COOKIE:
			slsi_util_nla_get_data(iter, hal_req->comebackinfo.cookie_length,
					       hal_req->comebackinfo.cookie);
			break;

		case NAN_REQ_ATTR_BOOTSTRAPPING_RESPONSE_CODE:
			slsi_util_nla_get_u8(iter, &hal_req->rsp_code);
			break;

		default:
			SLSI_INFO(sdev, "Unexpected NAN bootstrapping attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_bootstrapping_response(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	struct slsi_hal_nan_bootstrapping_rsp *hal_req;
	int ret = 0, i = 0;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;
	u16 transaction_id = 0;
	u32 bootstrapping_instance_id = 0;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "failed to alloc hal_req\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_bootstrapping_response_get_nl_params(sdev, hal_req, data, len);
	transaction_id = hal_req->transaction_id;
	if (reply_status) {
		kfree(hal_req);
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req->bootstrapping_instance_id ||
	    !slsi_nan_is_bootstrapping_instance_id_active(ndev_vif, hal_req->bootstrapping_instance_id)) {
		SLSI_WARN(sdev, "bootstrapping instance id %d not found.\n",
			  hal_req->bootstrapping_instance_id);
		for (i = 0; i < BITS_TO_LONGS(SLSI_NAN_MAX_BOOTSTRAPPING_INSTANCES); i++)
			SLSI_WARN(sdev, "Bootstrapping instance Id_Maps[%d]: 0x%x\n", i,
				  ndev_vif->nan.bootstrapping_instance_id_map[i]);
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_BOOTSTRAPPING_ID;
		ret = -EINVAL;
		goto exit_with_lock;
	}

	ret = slsi_mlme_nan_bootstrapping_response(sdev, dev, hal_req);
	if (ret) {
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		SLSI_INFO(sdev, "transId:%d bootId:%d Failed\n",
			  hal_req->transaction_id, hal_req->bootstrapping_instance_id);
	} else {
		bootstrapping_instance_id = hal_req->bootstrapping_instance_id;
		SLSI_INFO(sdev, "transId:%d bootstrappingId:%d\n", hal_req->transaction_id,
			  hal_req->bootstrapping_instance_id);
		if (hal_req->service_specific_info_len)
			SLSI_INFO_HEX(sdev, hal_req->service_specific_info,
				      hal_req->service_specific_info_len,
				      "service_specific_info\n");
	}

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(hal_req);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_BOOTSTRAPPING_RESPONDER_RESPONSE,
				      bootstrapping_instance_id, NULL, transaction_id, ndev_vif);
	return ret;
}

static int slsi_nan_pairing_req_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_pairing_req *hal_req,
					      const void *data, int len)
{
	int type, tmp, r;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;

		case NAN_REQ_ATTR_PAIRING_REQUESTOR_INSTANCE_ID:
			slsi_util_nla_get_u32(iter, &hal_req->requestor_instance_id);
			break;

		case NAN_REQ_ATTR_PAIRING_PEER_DISC_MAC_ADDR:
			slsi_util_nla_get_data(iter, ETH_ALEN, hal_req->peer_disc_mac_addr);
			break;

		case NAN_REQ_ATTR_PAIRING_REQUEST_TYPE:
			slsi_util_nla_get_u8(iter, &hal_req->nan_pairing_request_type);
			break;

		case NAN_REQ_ATTR_SECURITY_AKM:
			slsi_util_nla_get_u8(iter, &hal_req->akm);
			break;

		case NAN_REQ_ATTR_PAIRING_IS_OPPORTUNISTIC:
			slsi_util_nla_get_u8(iter, &hal_req->is_opportunistic);
			break;

		case NAN_REQ_ATTR_PAIRING_ENABLE_PAIRING_CACHE:
			slsi_util_nla_get_u8(iter, &hal_req->enable_pairing_cache);
			break;

		case NAN_REQ_ATTR_NIK:
			slsi_util_nla_get_data(iter, SLSI_HAL_NAN_IDENTITY_KEY_LEN, hal_req->nan_identity_key);
			break;

		default:
			r = slsi_nan_get_security_info_nl(sdev, &hal_req->sec_info, iter, type);
			if (r)
				SLSI_INFO(sdev, "Unexpected NAN pairing attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_pairing_request(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_hal_nan_pairing_req *hal_req;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	int ret, i = 0;
	u32 reply_status;
	u16 transaction_id = 0;
	u32 pairing_instance_id = 0;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "failed to alloc hal_req\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_pairing_req_get_nl_params(sdev, hal_req, data, len);
	transaction_id = hal_req->transaction_id;
	if (reply_status != SLSI_HAL_NAN_STATUS_SUCCESS) {
		kfree(hal_req);
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	hal_req->pairing_instance_id = slsi_nan_get_new_pairing_instance_id(ndev_vif);
	pairing_instance_id = hal_req->pairing_instance_id;
	if (pairing_instance_id) {
		struct slsi_vif_nan *nan_data;
		struct slsi_nan_discovery_info *nan_disc_info;

		nan_data = &ndev_vif->nan;
		nan_disc_info = nan_data->disc_info;

		while (nan_disc_info) {
			if (ether_addr_equal(nan_disc_info->peer_addr, hal_req->peer_disc_mac_addr)) {
				ether_addr_copy(ndev_vif->nan.pairing_id_peer_mac[pairing_instance_id - 1],
						hal_req->peer_disc_mac_addr);
				hal_req->session_id = nan_disc_info->session_id;
				SLSI_INFO(sdev, "transId:%d, pairing_instance_id:%d, session_id:%d\n",
					  transaction_id, pairing_instance_id, hal_req->session_id);
				break;
			}
			nan_disc_info = nan_disc_info->next;
		}

		ret = slsi_mlme_nan_pairing_request(sdev, dev, hal_req);
		if (ret) {
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
			SLSI_INFO(sdev, "transId:%d, pairing_instance_id:%d failed\n",
				  transaction_id, pairing_instance_id);
		} else {
			SLSI_INFO(sdev, "transId:%d, pairing_instance_id:%d\n",
				  transaction_id, pairing_instance_id);
			SLSI_INFO(sdev, "PASN Offloading Start\n");
		}
	} else {
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PAIRING_ID;
		SLSI_WARN(sdev, "Too Many PAIRING REQ\n");
		for (i = 0; i < BITS_TO_LONGS(SLSI_NAN_MAX_PAIRING_INSTANCES); i++)
			SLSI_WARN(sdev, "Pairing Id_Maps[%d]: 0x%x\n", i,
				  ndev_vif->nan.pairing_instance_id_map[i]);
		ret = -EOPNOTSUPP;
	}

	/* Store nan_pairing_request_type */
	ndev_vif->nan.pairing_confirm_ind.nan_pairing_request_type = hal_req->nan_pairing_request_type;

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(hal_req);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_PAIRING_INITIATOR_RESPONSE,
				      pairing_instance_id, NULL, transaction_id, ndev_vif);
	return ret;
}

static int slsi_nan_pairing_rsp_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_pairing_rsp *hal_req,
					      const void *data, int len)
{
	int type, tmp, r;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;

		case NAN_REQ_ATTR_PAIRING_INSTANCE_ID:
			slsi_util_nla_get_u32(iter, &hal_req->pairing_instance_id);
			break;

		case NAN_REQ_ATTR_PAIRING_IS_OPPORTUNISTIC:
			slsi_util_nla_get_u8(iter, &hal_req->is_opportunistic);
			break;

		case NAN_REQ_ATTR_PAIRING_ENABLE_PAIRING_CACHE:
			slsi_util_nla_get_u8(iter, &hal_req->enable_pairing_cache);
			break;

		case NAN_REQ_ATTR_NIK:
			slsi_util_nla_get_data(iter, SLSI_HAL_NAN_IDENTITY_KEY_LEN, hal_req->nan_identity_key);
			break;

		case NAN_REQ_ATTR_PAIRING_REQUEST_TYPE:
			slsi_util_nla_get_u8(iter, &hal_req->nan_pairing_request_type);
			break;

		case NAN_REQ_ATTR_PAIRING_RESPONSE_CODE:
			slsi_util_nla_get_u8(iter, &hal_req->rsp_code);
			break;

		case NAN_REQ_ATTR_SECURITY_AKM:
			slsi_util_nla_get_u8(iter, &hal_req->akm);
			break;

		default:
			r = slsi_nan_get_security_info_nl(sdev, &hal_req->sec_info, iter, type);
			if (r)
				SLSI_INFO(sdev, "Unexpected NAN pairing attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_pairing_response(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_hal_nan_pairing_rsp *hal_req;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	int ret = 0;
	u32 reply_status;
	u16 transaction_id = 0;
	u32 pairing_instance_id = 0;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "failed to alloc hal_req\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_pairing_rsp_get_nl_params(sdev, hal_req, data, len);
	transaction_id = hal_req->transaction_id;
	if (reply_status != SLSI_HAL_NAN_STATUS_SUCCESS) {
		kfree(hal_req);
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req->pairing_instance_id) {
		SLSI_WARN(sdev, "pairing instance id %d not found.\n", hal_req->pairing_instance_id);
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PAIRING_ID;
		ret = -EINVAL;
		goto exit_with_lock;
	}

	pairing_instance_id = hal_req->pairing_instance_id;
	ether_addr_copy(hal_req->peer_disc_mac_addr, ndev_vif->nan.pairing_response_req.peer_disc_mac_addr);
	hal_req->match_id = ndev_vif->nan.pairing_response_req.match_id;
	hal_req->session_id = ndev_vif->nan.pairing_response_req.session_id;
	if (hal_req->session_id) {
		ret = slsi_mlme_nan_pairing_response(sdev, dev, hal_req);
		if (ret) {
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
			SLSI_INFO(sdev, "transId:%d, pairing_instance_id:%d failed\n",
				  transaction_id, pairing_instance_id);
		} else {
			SLSI_INFO(sdev, "transId:%d, pairing_instance_id:%d\n",
				  transaction_id, pairing_instance_id);
			SLSI_INFO(sdev, "PASN Offloading Start\n");
		}
	}

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(hal_req);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_PAIRING_RESPONDER_RESPONSE,
				      pairing_instance_id, NULL, transaction_id, ndev_vif);
	return ret;
}

static int slsi_nan_pairing_end_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_pairing_end *hal_req,
					      const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;

		case NAN_REQ_ATTR_PAIRING_INSTANCE_ID:
			slsi_util_nla_get_u32(iter, &hal_req->pairing_instance_id);
			break;
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_pairing_end(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_hal_nan_pairing_end *hal_req;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	int ret = 0;
	u32 reply_status;
	u16 transaction_id = 0;
	u32 pairing_instance_id = 0;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "failed to alloc hal_req\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_pairing_end_get_nl_params(sdev, hal_req, data, len);
	pairing_instance_id = hal_req->pairing_instance_id;
	transaction_id = hal_req->transaction_id;
	if (reply_status != SLSI_HAL_NAN_STATUS_SUCCESS) {
		kfree(hal_req);
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (ndev_vif->nan.responder) {
		ether_addr_copy(hal_req->peer_disc_mac_addr, ndev_vif->nan.pairing_response_ind.peer_nmi_address);
		hal_req->session_id = ndev_vif->nan.pairing_response_ind.session_id;
	} else {
		ether_addr_copy(hal_req->peer_disc_mac_addr, ndev_vif->nan.pairing_request_ind.peer_nmi_address);
		hal_req->session_id = ndev_vif->nan.pairing_request_ind.session_id;
	}

	ret = slsi_mlme_nan_pairing_end(sdev, dev, hal_req);
	if (ret) {
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		SLSI_INFO(sdev, "transId:%d, pairing_instance_id:%d failed\n",
			  transaction_id, pairing_instance_id);
	} else {
		SLSI_INFO(sdev, "transId:%d, pairing_instance_id:%d\n",
			  transaction_id, pairing_instance_id);
	}

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(hal_req);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_PAIRING_END,
				      pairing_instance_id, NULL, transaction_id, ndev_vif);
	return ret;
}

int slsi_nan_pasn_send_mlme(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	u16 host_tag = slsi_tx_mgmt_host_tag(sdev);
	struct netdev_vif *ndev_vif = NULL;
	const struct nlattr *iter;
	int type, tmp;
	struct slsi_nan_pasn_send_mlme_params *send_params = NULL;
	int ret = 0;

	SLSI_INFO(sdev, "[NAN][Pairing][Driver]slsi nan pasn send mlme start\n");

	send_params = kmalloc(sizeof(*send_params), GFP_KERNEL);
	if (!send_params) {
		SLSI_ERR(sdev, "failed to alloc hal_req\n");
		ret = -ENOMEM;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	memset(send_params, 0, sizeof(*send_params));

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_PASN_SEND_MLME_LEN:
			slsi_util_nla_get_u16(iter, &send_params->frame_len);
			break;
		case NAN_REQ_ATTR_PASN_SEND_MLME:
			slsi_util_nla_get_data(iter, send_params->frame_len, send_params->frame);
			break;
		default:
			SLSI_INFO(sdev, "Unexpected NAN_PASN attribute TYPE:%d\n", type);
		}
	}

	SLSI_INFO(sdev, "[NAN][Pairing][Driver]sdev->send_mlme_params.frame_len:%d\n", send_params->frame_len);
	SCSC_BIN_TAG_INFO(BINARY, send_params->frame, send_params->frame_len);
	ret = slsi_mlme_nan_send_frame_mgmt(sdev, dev, send_params->frame, send_params->frame_len,
					    FAPI_DATAUNITDESCRIPTOR_IEEE802_11_FRAME, FAPI_MESSAGETYPE_IEEE80211_MGMT,
					    host_tag, 0, 0, 0);

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(send_params);
exit:
	SLSI_INFO(sdev, "[NAN][Pairing][Driver] NAN_PASN send ret:%d\n", ret);
	return ret;
}

#endif

static int slsi_nan_config_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_config_req *hal_req,
					 const void *data, int len)
{
	int type, type1, tmp, tmp1, disc_attr_idx = 0, famchan_idx = 0;
	const struct nlattr *iter, *iter1;
	struct slsi_hal_nan_post_discovery_param *disc_attr;
	struct slsi_hal_nan_further_availability_channel *famchan;
#ifdef SCSC_SEP_VERSION
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
#endif
	u8 val = 0;
	u32 random_interval = 0, instant_mode = 0, instant_mode_channel = 0;

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_SID_BEACON_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->sid_beacon);
			hal_req->config_sid_beacon = 1;
			break;

		case NAN_REQ_ATTR_RSSI_PROXIMITY_2G4_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_proximity);
			hal_req->config_rssi_proximity = 1;
			break;

		case NAN_REQ_ATTR_MASTER_PREF:
			slsi_util_nla_get_u8(iter, &hal_req->master_pref);
			hal_req->config_master_pref = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_PROXIMITY_5G_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->rssi_close_proximity_5g_val);
			hal_req->config_5g_rssi_close_proximity = 1;
			break;

		case NAN_REQ_ATTR_RSSI_WINDOW_SIZE_VAL:
			slsi_util_nla_get_u8(iter, &val);
			hal_req->rssi_window_size_val = (u16)val;
			hal_req->config_rssi_window_size = 1;
			break;

		case NAN_REQ_ATTR_CLUSTER_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->config_cluster_attribute_val);
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_DWELL_TIME:
			slsi_util_nla_get_data(iter, sizeof(hal_req->scan_params_val.dwell_time),
					       hal_req->scan_params_val.dwell_time);
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_PERIOD:
			slsi_util_nla_get_data(iter, sizeof(hal_req->scan_params_val.scan_period),
					       hal_req->scan_params_val.scan_period);
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_RANDOM_FACTOR_FORCE_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->random_factor_force_val);
			hal_req->config_random_factor_force = 1;
			break;

		case NAN_REQ_ATTR_HOP_COUNT_FORCE_VAL:
			slsi_util_nla_get_u8(iter, &hal_req->hop_count_force_val);
			hal_req->config_hop_count_force = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_PAYLOAD_TX:
			slsi_util_nla_get_u8(iter, &hal_req->conn_capability_val.payload_transmit_flag);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_WFD:
			slsi_util_nla_get_u8(iter, &hal_req->conn_capability_val.is_wfd_supported);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_WFDS:
			slsi_util_nla_get_u8(iter, &hal_req->conn_capability_val.is_wfds_supported);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_TDLS:
			slsi_util_nla_get_u8(iter, &hal_req->conn_capability_val.is_tdls_supported);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_MESH:
			slsi_util_nla_get_u8(iter, &hal_req->conn_capability_val.is_mesh_supported);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_IBSS:
			slsi_util_nla_get_u8(iter, &hal_req->conn_capability_val.is_ibss_supported);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_WLAN_INFRA:
			slsi_util_nla_get_u8(iter, &hal_req->conn_capability_val.wlan_infra_field);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_DISCOVERY_ATTR_NUM_ENTRIES:
			slsi_util_nla_get_u8(iter, &hal_req->num_config_discovery_attr);
			break;

		case NAN_REQ_ATTR_DISCOVERY_ATTR_VAL:
			if (hal_req->num_config_discovery_attr > SLSI_HAL_NAN_MAX_POSTDISCOVERY_LEN)
				return -EINVAL;
			if (disc_attr_idx >= hal_req->num_config_discovery_attr) {
				SLSI_ERR(sdev,
					 "disc attr(%d) > num disc attr(%d)\n",
					 disc_attr_idx + 1, hal_req->num_config_discovery_attr);
				return -EINVAL;
			}
			disc_attr = &hal_req->discovery_attr_val[disc_attr_idx];
			disc_attr_idx++;
			nla_for_each_nested(iter1, iter, tmp1) {
				type1 = nla_type(iter1);
				switch (type1) {
				case NAN_REQ_ATTR_CONN_TYPE:
					slsi_util_nla_get_u8(iter1, &disc_attr->type);
					break;

				case NAN_REQ_ATTR_NAN_ROLE:
					slsi_util_nla_get_u8(iter1, &disc_attr->role);
					break;

				case NAN_REQ_ATTR_TRANSMIT_FREQ:
					slsi_util_nla_get_u8(iter1, &disc_attr->transmit_freq);
					break;

				case NAN_REQ_ATTR_AVAILABILITY_DURATION:
					slsi_util_nla_get_u8(iter1, &disc_attr->duration);
					break;

				case NAN_REQ_ATTR_AVAILABILITY_INTERVAL:
					slsi_util_nla_get_u32(iter1, &disc_attr->avail_interval_bitmap);
					break;

				case NAN_REQ_ATTR_MAC_ADDR_VAL:
					slsi_util_nla_get_data(iter1, ETH_ALEN, disc_attr->addr);
					break;

				case NAN_REQ_ATTR_MESH_ID_LEN:
					slsi_util_nla_get_u16(iter1, &disc_attr->mesh_id_len);
					break;

				case NAN_REQ_ATTR_MESH_ID:
					if (disc_attr->mesh_id_len > SLSI_HAL_NAN_MAX_MESH_DATA_LEN)
						return -EINVAL;
					slsi_util_nla_get_data(iter1, disc_attr->mesh_id_len, disc_attr->mesh_id);
					break;

				case NAN_REQ_ATTR_INFRASTRUCTURE_SSID_LEN:
					slsi_util_nla_get_u16(iter1, &disc_attr->infrastructure_ssid_len);
					break;

				case NAN_REQ_ATTR_INFRASTRUCTURE_SSID:
					if (disc_attr->infrastructure_ssid_len > SLSI_HAL_NAN_MAX_INFRA_DATA_LEN)
						return -EINVAL;
					slsi_util_nla_get_data(iter1, disc_attr->infrastructure_ssid_len,
							       disc_attr->infrastructure_ssid_val);
					break;
				}
			}
			break;

		case NAN_REQ_ATTR_FURTHER_AVAIL_NUM_ENTRIES:
			slsi_util_nla_get_u8(iter, &hal_req->fam_val.numchans);
			hal_req->config_fam = 1;
			break;

		case NAN_REQ_ATTR_FURTHER_AVAIL_VAL:
			hal_req->config_fam = 1;
			if (hal_req->fam_val.numchans > SLSI_HAL_NAN_MAX_FAM_CHANNELS)
				return -EINVAL;
			if (famchan_idx >= hal_req->fam_val.numchans) {
				SLSI_ERR(sdev,
					 "famchan attr(%d) > numchans(%d)\n",
					 famchan_idx + 1, hal_req->fam_val.numchans);
				return -EINVAL;
			}
			famchan = &hal_req->fam_val.famchan[famchan_idx];
			famchan_idx++;
			nla_for_each_nested(iter1, iter, tmp1) {
				type1 = nla_type(iter1);
				switch (type1) {
				case NAN_REQ_ATTR_FURTHER_AVAIL_ENTRY_CTRL:
					slsi_util_nla_get_u8(iter1, &famchan->entry_control);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_CHAN_CLASS:
					slsi_util_nla_get_u8(iter1, &famchan->class_val);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_CHAN:
					slsi_util_nla_get_u8(iter1, &famchan->channel);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_CHAN_MAPID:
					slsi_util_nla_get_u8(iter1, &famchan->mapid);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_INTERVAL_BITMAP:
					slsi_util_nla_get_u32(iter1, &famchan->avail_interval_bitmap);
					break;
				}
			}
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SID_BEACON_VAL:
			slsi_util_nla_get_u8(iter, &val);
			hal_req->subscribe_sid_beacon_val = (u32)val;
			hal_req->config_subscribe_sid_beacon = 1;
			break;

		case NAN_REQ_ATTR_DW_2G4_INTERVAL:
			slsi_util_nla_get_u8(iter, &val);
			hal_req->dw_2dot4g_interval_val = (u32)val;
			/* valid range for 2.4G is 1-5 */
			if (hal_req->dw_2dot4g_interval_val > 0  && hal_req->dw_2dot4g_interval_val < 6)
				hal_req->config_2dot4g_dw_band = 1;
			break;

		case NAN_REQ_ATTR_DW_5G_INTERVAL:
			slsi_util_nla_get_u8(iter, &val);
			hal_req->dw_5g_interval_val = (u32)val;
			/* valid range for 5g is 0-5 */
			if (hal_req->dw_5g_interval_val < 6)
				hal_req->config_5g_dw_band = 1;
			break;
		case NAN_REQ_ATTR_DISC_MAC_ADDR_RANDOM_INTERVAL:
			slsi_util_nla_get_u32(iter, &random_interval);
			if (slsi_get_nan_mac_random())
				hal_req->disc_mac_addr_rand_interval_sec = random_interval;
			else
				hal_req->disc_mac_addr_rand_interval_sec = 0;
#ifdef SCSC_SEP_VERSION
			if (random_interval & BIT(31)) {
				hal_req->disc_mac_addr_rand_interval_sec &= SLSI_NAN_MAC_RANDOM_INTERVAL_MASK;
				ndev_vif->nan.disable_cluster_merge = (random_interval & BIT(30)) ? 0 : 1;
				slsi_set_mib_fast_connect_enabled(sdev, (random_interval & BIT(29)) ? 1 : 0);
			}
#endif
			break;
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;
		case NAN_REQ_ATTR_DISCOVERY_BEACON_INT:
			slsi_util_nla_get_u32(iter, &hal_req->discovery_beacon_interval_ms);
			break;
		case NAN_REQ_ATTR_NSS:
			slsi_util_nla_get_u32(iter, &hal_req->nss_discovery);
			break;
		case NAN_REQ_ATTR_ENABLE_RANGING:
			slsi_util_nla_get_u32(iter, &hal_req->enable_ranging);
			break;
		case NAN_REQ_ATTR_DW_EARLY_TERMINATION:
			slsi_util_nla_get_u32(iter, &hal_req->enable_dw_early_termination);
			break;
		case NAN_REQ_ATTR_ENABLE_INSTANT_MODE:
			slsi_util_nla_get_u32(iter, &instant_mode);
			hal_req->enable_instant_mode = (u16)instant_mode;
			break;
		case NAN_REQ_ATTR_INSTANT_MODE_CHANNEL:
			slsi_util_nla_get_u32(iter, &instant_mode_channel);
			hal_req->instant_mode_channel = (u16)instant_mode_channel;
			break;
		default:
			SLSI_INFO(sdev, "Unexpected NAN config attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_set_config(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	u16 transaction_id = 0;
	int ret;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	reply_status = slsi_nan_config_get_nl_params(sdev, &ndev_vif->nan.config, data, len);
	transaction_id = ndev_vif->nan.config.transaction_id;
	if (reply_status) {
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
		ret = -EINVAL;
		goto exit;
	}
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
	} else {
		ret = slsi_mlme_nan_set_config(sdev, dev, &ndev_vif->nan.config);
		if (ret) {
			if (ret == FAPI_RESULTCODE_INVALID_PARAMETERS)
				reply_status = SLSI_HAL_NAN_STATUS_INVALID_PARAM;
			else
				reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		} else {
			if (ndev_vif->nan.config.config_master_pref)
				ndev_vif->nan.master_pref_value = ndev_vif->nan.config.master_pref;
			ndev_vif->nan.random_mac_interval_sec = ndev_vif->nan.config.disc_mac_addr_rand_interval_sec;
			SLSI_INFO(sdev, "transId:%d masterPref:%d\n",
				  ndev_vif->nan.config.transaction_id, ndev_vif->nan.config.master_pref);
		}
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_CONFIG, 0, NULL, transaction_id, ndev_vif);
	return 0;
}

int slsi_nan_get_capabilities(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;
	struct slsi_hal_nan_capabilities nan_capabilities;
	int ret = 0, i;
	struct slsi_mib_value *values = NULL;
	struct slsi_mib_data mibrsp = { 0, NULL };
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_NAN_MAX_CONCURRENT_CLUSTERS, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_CONCURRENT_PUBLISHES, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_CONCURRENT_SUBSCRIBES, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_SERVICE_NAME_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_MATCH_FILTER_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_TOTAL_MATCH_FILTER_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_SERVICE_SPECIFIC_INFO_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_NDP_SESSIONS, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_APP_INFO_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_QUEUED_FOLLOWUPS, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_SUBSCRIBE_INTERFACE_ADDRESSES, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_SUPPORTED_CIPHER_SUITES, { 0, 0 }, },
						  { SLSI_PSID_UNIFI_NAN_MAX_EXTENDED_SERVICE_SPECIFIC_INFO_LEN, { 0, 0} },
						  { SLSI_PSID_UNIFI_NAN_INSTANT_COMM_SUPPORTED, { 0, 0} } };
	u32 *capabilities_mib_val[] = { &nan_capabilities.max_concurrent_nan_clusters,
					&nan_capabilities.max_publishes,
					&nan_capabilities.max_subscribes,
					&nan_capabilities.max_service_name_len,
					&nan_capabilities.max_match_filter_len,
					&nan_capabilities.max_total_match_filter_len,
					&nan_capabilities.max_service_specific_info_len,
					&nan_capabilities.max_ndp_sessions,
					&nan_capabilities.max_app_info_len,
					&nan_capabilities.max_queued_transmit_followup_msgs,
					&nan_capabilities.max_subscribe_address,
					&nan_capabilities.cipher_suites_supported,
					&nan_capabilities.max_sdea_service_specific_info_len,
					&nan_capabilities.instant_comm_supported };
	int type, tmp;
	const struct nlattr *iter;
	u16 transaction_id = 0;

	memset(&nan_capabilities, 0, sizeof(nan_capabilities));
	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &(transaction_id));
			break;
		default:
			break;
		}
	}

	ndev_vif = netdev_priv(dev);

	/* Expect each mib length in response is 11 */
	mibrsp.dataLength = 11 * ARRAY_SIZE(get_values);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	values = slsi_read_mibs(sdev, NULL, get_values, ARRAY_SIZE(get_values), &mibrsp);
	if (!values)
		goto exit_with_mibrsp;

	for (i = 0; i < (int)ARRAY_SIZE(get_values); i++) {
		if (values[i].type == SLSI_MIB_TYPE_UINT) {
			*capabilities_mib_val[i] = values[i].u.uintValue;
			SLSI_DBG3(sdev, SLSI_GSCAN, "MIB value = %u\n", *capabilities_mib_val[i]);
		} else {
			SLSI_ERR(sdev, "invalid type(%d). iter:%d\n", values[i].type, i);
			*capabilities_mib_val[i] = 0;
		}
	}

	nan_capabilities.max_ndi_interfaces = slsi_get_nan_max_ndi_ifaces();
	if (!nan_capabilities.max_ndp_sessions)
		nan_capabilities.max_ndp_sessions = slsi_get_nan_max_ndp_instances();

	if (nan_capabilities.max_ndi_interfaces > SLSI_NAN_MAX_NDP_INSTANCES) {
		SLSI_ERR(sdev, "max ndp if's:%d but supported:%d\n", nan_capabilities.max_ndi_interfaces,
			 SLSI_NAN_MAX_NDP_INSTANCES);
		nan_capabilities.max_ndi_interfaces = SLSI_NAN_MAX_NDP_INSTANCES;
	}

	if (nan_capabilities.max_ndp_sessions > SLSI_NAN_MAX_NDP_INSTANCES) {
		SLSI_ERR(sdev, "max ndp if's:%d but supported:%d\n", nan_capabilities.max_ndp_sessions,
			 SLSI_NAN_MAX_NDP_INSTANCES);
		nan_capabilities.max_ndp_sessions = SLSI_NAN_MAX_NDP_INSTANCES;
	}
	/* Currently Firmware and driver supports only one follow up message per peer at the same time */
	nan_capabilities.max_queued_transmit_followup_msgs = 1;
	SLSI_INFO(sdev, "transId:%d\n", transaction_id);

	kfree(values);
exit_with_mibrsp:
	kfree(mibrsp.data);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	/* Always return success to HAL */
	slsi_vendor_nan_command_reply(wiphy, SLSI_HAL_NAN_STATUS_SUCCESS, 0, NAN_RESPONSE_GET_CAPABILITIES, 0, &nan_capabilities, transaction_id, ndev_vif);
	return 0;
}

void slsi_vendor_nan_event_create_delete(struct slsi_dev *sdev, int hal_event, int transaction_id, int reply_status)
{

	struct sk_buff *nl_skb = NULL;
	int res = 0;

#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, hal_event, GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, hal_event, GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "Failed to allocate skb for nan_event_create\n");
		return;
	}

	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_STATUS, reply_status);
	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_HAL_TRANSACTION_ID, transaction_id);
	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:0x%x\n", res);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		return;
	}
	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
}

#if (KERNEL_VERSION(5, 12, 0) <= LINUX_VERSION_CODE)
int slsi_nan_data_iface_create(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	u8 iface_name[IFNAMSIZ] = {0};
	int ret = 0, reply_status, type, tmp;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	const struct nlattr *iter;
	u16 transaction_id = 0;
	struct slsi_nan_data_iface_create_delete *intfdata = NULL;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		if (type == NAN_REQ_ATTR_DATA_INTERFACE_NAME) {
			/* 16 is the interface length from net_device
			 * structure.
			 */
			if (nla_len(iter) > IFNAMSIZ)
				return -EINVAL;
			memcpy(iface_name, nla_data(iter), nla_len(iter));
			SLSI_ENSURE_NULL_TERMINATED(iface_name, sizeof(iface_name));
		} else if (type == NAN_REQ_ATTR_HAL_TRANSACTION_ID)
			slsi_util_nla_get_u16(iter, &transaction_id);
	}

	if (!iface_name[0]) {
		SLSI_ERR(sdev, "No NAN data interface name\n");
		ret = WIFI_HAL_ERROR_INVALID_ARGS;
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		goto exit;
	}
	intfdata = kzalloc(sizeof(*intfdata), GFP_KERNEL);
	if (!intfdata) {
		SLSI_NET_ERR(dev, "Data Interface Create Failed");
		return -EINVAL;
	}
	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	memcpy(intfdata->ifname, iface_name, strlen(iface_name));
	intfdata->transaction_id = transaction_id;
	intfdata->iface_action = NAN_IFACE_CREATE;
	list_add(&intfdata->list, &sdev->nan_data_interface_create_delete_data);
	schedule_work(&sdev->nan_data_interface_create_delete_work);
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
exit:
	if (ret != WIFI_HAL_SUCCESS)
		slsi_vendor_nan_command_reply(sdev->wiphy, reply_status, ret, NAN_DP_INTERFACE_CREATE, 0, NULL, transaction_id, NULL);
	return ret;
}
#else
int slsi_nan_data_iface_create(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	u8 iface_name[IFNAMSIZ] = {0};
	int ret = 0, if_idx, type, tmp, err;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct net_device *dev_ndp = NULL;
	struct netdev_vif *ndev_data_vif = NULL;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;
	const struct nlattr *iter;
	u16 transaction_id = 0;
	bool is_cfg80211 = false;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		if (type == NAN_REQ_ATTR_DATA_INTERFACE_NAME) {
			/* 16 is the interface length from net_device
			 * structure.
			 */
			if (nla_len(iter) > IFNAMSIZ)
				return -EINVAL;
			memcpy(iface_name, nla_data(iter), nla_len(iter));
			SLSI_ENSURE_NULL_TERMINATED(iface_name, sizeof(iface_name));
		} else if (type == NAN_REQ_ATTR_HAL_TRANSACTION_ID)
			slsi_util_nla_get_u16(iter, &transaction_id);
	}
	if (!iface_name[0]) {
		SLSI_ERR(sdev, "No NAN data interface name\n");
		ret = WIFI_HAL_ERROR_INVALID_ARGS;
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		goto exit;
	}

	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	/* Find unused netdev idx */
	for (if_idx = SLSI_NAN_DATA_IFINDEX_START; if_idx < CONFIG_SCSC_WLAN_MAX_INTERFACES + 1; if_idx++) {
		dev_ndp = slsi_get_netdev_locked(sdev, if_idx);
		if (!dev_ndp)
			break;
	}

	if (if_idx >= CONFIG_SCSC_WLAN_MAX_INTERFACES + 1) {
		SLSI_ERR(sdev, "NAN no free NAN data interfaces\n");
		ret = WIFI_HAL_ERROR_TOO_MANY_REQUESTS;
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		goto exit_with_lock;
	}

	err = slsi_netif_add_locked(sdev, iface_name, if_idx);
	if (err) {
		SLSI_ERR(sdev, "NAN fail net_if_add if_name:%s, if_idx:%d\n", iface_name, if_idx);
		ret = WIFI_HAL_ERROR_OUT_OF_MEMORY;
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		goto exit_with_lock;
	}

	dev_ndp = slsi_get_netdev_locked(sdev, if_idx);
	if (!dev_ndp)
		goto exit_with_lock;

	err = slsi_netif_register_locked(sdev, dev_ndp, is_cfg80211);
	if (err) {
		SLSI_ERR(sdev, "NAN fail netdev err:%d if_name:%s, if_idx:%d\n", err);
		ret = WIFI_HAL_ERROR_UNKNOWN;
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
	} else {
		SLSI_INFO(sdev, "trans_id:%d, if_name:%s, if_idx:%d\n", transaction_id, iface_name, if_idx);
		ndev_data_vif = netdev_priv(dev_ndp);
		SLSI_MUTEX_LOCK(ndev_data_vif->vif_mutex);
		ndev_data_vif->vif_type = SLSI_NAN_VIF_TYPE_NDP;
		SLSI_MUTEX_UNLOCK(ndev_data_vif->vif_mutex);
	}

exit_with_lock:
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_DP_INTERFACE_CREATE, 0, NULL, transaction_id, ndev_data_vif);
	return ret;
}
#endif

#if (KERNEL_VERSION(5, 12, 0) <= LINUX_VERSION_CODE)
int slsi_nan_data_iface_delete(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	u8 iface_name[IFNAMSIZ] = {0};
	int ret = 0, type, tmp;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;
	const struct nlattr *iter;
	u16 transaction_id = 0;
	struct slsi_nan_data_iface_create_delete *intfdata = NULL;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		if (type == NAN_REQ_ATTR_DATA_INTERFACE_NAME) {
			/* 16 is the interface length from net_device
			 * structure.
			 */
			if (nla_len(iter) > IFNAMSIZ)
				return -EINVAL;
			memcpy(iface_name, nla_data(iter), nla_len(iter));
			SLSI_ENSURE_NULL_TERMINATED(iface_name, sizeof(iface_name));
		} else if (type == NAN_REQ_ATTR_HAL_TRANSACTION_ID)
			slsi_util_nla_get_u16(iter, &(transaction_id));
	}
	if (!iface_name[0]) {
		SLSI_ERR(sdev, "No NAN data interface name\n");
		ret = WIFI_HAL_ERROR_INVALID_ARGS;
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		goto exit;
	}
	intfdata = kzalloc(sizeof(*intfdata), GFP_KERNEL);
	if (!intfdata) {
		SLSI_NET_ERR(dev, "Data Interface delete Failed");
		return -EINVAL;
	}

	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	memcpy(intfdata->ifname, iface_name, strlen(iface_name));
	SLSI_ERR(sdev, "NAN  delete intf if_name:%s\n", iface_name);
	intfdata->transaction_id = transaction_id;
	intfdata->iface_action = NAN_IFACE_DELETE;
	list_add(&intfdata->list, &sdev->nan_data_interface_create_delete_data);
	schedule_work(&sdev->nan_data_interface_create_delete_work);
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
exit:
	if (ret != WIFI_HAL_SUCCESS)
		slsi_vendor_nan_command_reply(sdev->wiphy, reply_status, ret, NAN_DP_INTERFACE_DELETE, 0, NULL, transaction_id, NULL);
	return ret;
}
#else
int slsi_nan_data_iface_delete(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	u8 iface_name[IFNAMSIZ] = {0};
	int ret = 0, if_idx, type, tmp;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct net_device *dev_ndp = NULL;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;
	const struct nlattr *iter;
	u16 transaction_id = 0;
	bool is_cfg80211 = false;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		if (type == NAN_REQ_ATTR_DATA_INTERFACE_NAME) {
			/* 16 is the interface length from net_device
			 * structure.
			 */
			if (nla_len(iter) > IFNAMSIZ)
				return -EINVAL;
			memcpy(iface_name, nla_data(iter), nla_len(iter));
			SLSI_ENSURE_NULL_TERMINATED(iface_name, sizeof(iface_name));
		} else if (type == NAN_REQ_ATTR_HAL_TRANSACTION_ID)
			slsi_util_nla_get_u16(iter, &(transaction_id));
	}
	if (!iface_name[0]) {
		SLSI_ERR(sdev, "No NAN data interface name\n");
		ret = WIFI_HAL_ERROR_INVALID_ARGS;
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		goto exit;
	}

	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	for (if_idx = SLSI_NAN_DATA_IFINDEX_START; if_idx < CONFIG_SCSC_WLAN_MAX_INTERFACES + 1; if_idx++) {
		dev_ndp = slsi_get_netdev_locked(sdev, if_idx);
		if (dev_ndp && strcmp(iface_name, dev_ndp->name) == 0)
			break;
		dev_ndp = NULL;
	}

	if (dev_ndp) {
		slsi_netif_remove_locked(sdev, dev_ndp, is_cfg80211);
		SLSI_INFO(sdev, "Success transId:%d ifaceName:%s\n", transaction_id, iface_name);
	}

	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_DP_INTERFACE_DELETE, 0, NULL, transaction_id, NULL);
	return ret;
}
#endif

#if (KERNEL_VERSION(5, 12, 0) <= LINUX_VERSION_CODE)
static inline void
slsi_nan_del_aware_intf_node(struct slsi_dev *sdev, struct slsi_nan_data_iface_create_delete *aware_intf, int nan_event,
			     int transaction_id, u32 reply_status)
{
	list_del(&aware_intf->list);
	kfree(aware_intf);
	slsi_vendor_nan_event_create_delete(sdev, nan_event, transaction_id, reply_status);
}

void slsi_nan_data_interface_create_delete_work(struct work_struct *work)
{
	struct slsi_dev *sdev = container_of(work, struct slsi_dev, nan_data_interface_create_delete_work);
	int if_idx;
	struct net_device *dev_ndp = NULL;
	struct netdev_vif *ndev_data_vif = NULL;
	bool is_cfg80211 = true;
	struct slsi_nan_data_iface_create_delete *aware_intf, *tmp;
	int transaction_id = 0;
	int err, nan_event;

	rtnl_lock();
	mutex_lock(&sdev->wiphy->mtx);
	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	SLSI_INFO(sdev, "In Data Interface create delete Work Queue\n");
	list_for_each_entry_safe(aware_intf, tmp, &sdev->nan_data_interface_create_delete_data, list) {
		if (!aware_intf)
			goto exit_with_lock;

		transaction_id = aware_intf->transaction_id;

		if (aware_intf->iface_action == NAN_IFACE_CREATE) {
			nan_event = SLSI_NL80211_NAN_INTERFACE_CREATED_EVENT;
			/* Find unused netdev idx */
			for (if_idx = SLSI_NAN_DATA_IFINDEX_START; if_idx < CONFIG_SCSC_WLAN_MAX_INTERFACES + 1; if_idx++) {
				dev_ndp = slsi_get_netdev_locked(sdev, if_idx);
				if (!dev_ndp)
					break;
			}

			if (if_idx >= CONFIG_SCSC_WLAN_MAX_INTERFACES + 1) {
				SLSI_ERR(sdev, "NAN no free NAN data interfaces\n");
				slsi_nan_del_aware_intf_node(sdev, aware_intf, nan_event, transaction_id,
							     SLSI_HAL_NAN_STATUS_INVALID_PARAM);
				continue;
			}

			err = slsi_netif_add_locked(sdev, aware_intf->ifname, if_idx);
			if (err) {
				SLSI_ERR(sdev, "NAN fail net_if_add if_name:%s, if_idx:%d\n", aware_intf->ifname, if_idx);
				slsi_nan_del_aware_intf_node(sdev, aware_intf, nan_event, transaction_id,
							     SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE);
				continue;
			}

			dev_ndp = slsi_get_netdev_locked(sdev, if_idx);
			if (!dev_ndp) {
				SLSI_ERR(sdev, "NAN failed to get netdev for index: %d\n", if_idx);
				slsi_nan_del_aware_intf_node(sdev, aware_intf, nan_event, transaction_id,
							     SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE);
				continue;
			}

			err = slsi_netif_register_locked(sdev, dev_ndp, is_cfg80211);
			if (err) {
				SLSI_ERR(sdev, "NAN fail netdev err:%d if_name:%s, if_idx:%d\n", err);
				slsi_nan_del_aware_intf_node(sdev, aware_intf, nan_event, transaction_id,
							     SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE);
				continue;
			} else {
				SLSI_INFO(sdev, "Create: trans_id:%d, if_name:%s, if_idx:%d\n", transaction_id, aware_intf->ifname, if_idx);
				ndev_data_vif = netdev_priv(dev_ndp);
				SLSI_MUTEX_LOCK(ndev_data_vif->vif_mutex);
				ndev_data_vif->vif_type = SLSI_NAN_VIF_TYPE_NDP;
				SLSI_MUTEX_UNLOCK(ndev_data_vif->vif_mutex);
			}
		} else {
			nan_event = SLSI_NL80211_NAN_INTERFACE_DELETED_EVENT;
			for (if_idx = SLSI_NAN_DATA_IFINDEX_START; if_idx < CONFIG_SCSC_WLAN_MAX_INTERFACES + 1; if_idx++) {
				dev_ndp = slsi_get_netdev_locked(sdev, if_idx);
				if (dev_ndp && strcmp(aware_intf->ifname, dev_ndp->name) == 0)
					break;
				dev_ndp = NULL;
			}

			if (dev_ndp) {
				slsi_netif_remove_locked(sdev, dev_ndp, is_cfg80211);
				SLSI_INFO(sdev, "Delete: Success transId:%d ifaceName:%s\n", transaction_id, aware_intf->ifname);
			} else {
				SLSI_ERR(sdev, "Delete: Failure transId:%d ifaceName:%s\n", transaction_id, aware_intf->ifname);
			}
		}
		slsi_nan_del_aware_intf_node(sdev, aware_intf, nan_event, transaction_id, SLSI_HAL_NAN_STATUS_SUCCESS);
	}
	INIT_LIST_HEAD(&sdev->nan_data_interface_create_delete_data);

exit_with_lock:
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
	mutex_unlock(&sdev->wiphy->mtx);
	rtnl_unlock();
}
#endif

int slsi_nan_ndp_initiate_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_data_path_initiator_req *hal_req,
					const void *data, int len)
{
	int type, tmp, r;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_REQ_INSTANCE_ID:
			slsi_util_nla_get_u32(iter, &hal_req->requestor_instance_id);
			break;

		case NAN_REQ_ATTR_CHAN_REQ_TYPE:
			slsi_util_nla_get_u8(iter, &hal_req->channel_request_type);
			break;

		case NAN_REQ_ATTR_CHAN:
			slsi_util_nla_get_u32(iter, &hal_req->channel);
			break;

		case NAN_REQ_ATTR_MAC_ADDR_VAL:
			if (nla_len(iter) < ETH_ALEN)
				return -EINVAL;
			ether_addr_copy(hal_req->peer_disc_mac_addr, nla_data(iter));
			break;

		case NAN_REQ_ATTR_DATA_INTERFACE_NAME:
			slsi_util_nla_get_data(iter, IFNAMSIZ, hal_req->ndp_iface);
			break;

		case NAN_REQ_ATTR_DATA_INTERFACE_NAME_LEN:
			break;

		case NAN_REQ_ATTR_SDEA_PARAM_SECURITY_CFG:
			slsi_util_nla_get_u8(iter, &hal_req->ndp_cfg.security_cfg);
			break;

		case NAN_REQ_ATTR_SDEA_PARAM_QOS_CFG:
			slsi_util_nla_get_u8(iter, &hal_req->ndp_cfg.qos_cfg);
			break;

		case NAN_REQ_ATTR_APP_INFO_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->app_info.ndp_app_info_len);
			break;

		case NAN_REQ_ATTR_APP_INFO:
			if (hal_req->app_info.ndp_app_info_len > SLSI_HAL_NAN_DP_MAX_APP_INFO_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->app_info.ndp_app_info_len,
					       hal_req->app_info.ndp_app_info);
			break;

		case NAN_REQ_ATTR_SERVICE_NAME_LEN:
			slsi_util_nla_get_u32(iter, &hal_req->service_name_len);
			break;

		case NAN_REQ_ATTR_SERVICE_NAME:
			slsi_util_nla_get_data(iter, hal_req->service_name_len, hal_req->service_name);
			break;

		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;

		default:
			r = slsi_nan_get_security_info_nl(sdev, &hal_req->key_info, iter, type);
			if (r)
				SLSI_INFO(sdev, "Unexpected NAN ndp attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_ndp_initiate(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	struct slsi_hal_nan_data_path_initiator_req *hal_req = NULL;
	int ret;
	u32 ndp_instance_id = 0;
	u16 ndl_vif_id, transaction_id = 0;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "Failed to alloc hal_req structure!!!\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = WIFI_HAL_ERROR_OUT_OF_MEMORY;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_ndp_initiate_get_nl_params(sdev, hal_req, data, len);
	transaction_id = hal_req->transaction_id;
	if (reply_status) {
		ret = WIFI_HAL_ERROR_INVALID_ARGS;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	ndp_instance_id = slsi_nan_get_new_ndp_instance_id(ndev_vif);

	if (!ndp_instance_id) {
		SLSI_WARN(sdev, "NAN no free ndp slots\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = WIFI_HAL_ERROR_TOO_MANY_REQUESTS;
		goto exit_with_lock;
	}

	ndev_vif->nan.next_ndp_instance_id = (ndp_instance_id + 1) % (SLSI_NAN_MAX_NDP_INSTANCES + 1);
	ndl_vif_id = slsi_nan_ndp_get_ndl_vif_id(sdev, hal_req->peer_disc_mac_addr, ndev_vif->nan.ndl_list);
	if (ndl_vif_id >= SLSI_NAN_MAX_NDP_INSTANCES + (FAPI_VIFRANGE_VIF_INDEX_MAX - sdev->nan_max_ndp_instances + 1)) {
		SLSI_WARN(sdev, "NAN no free ndl slots\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = WIFI_HAL_ERROR_TOO_MANY_REQUESTS;
		goto exit_with_lock;
	}
	ret = slsi_mlme_ndp_request(sdev, dev, hal_req, ndp_instance_id, ndl_vif_id);
	if (ret) {
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		ret = WIFI_HAL_ERROR_UNKNOWN;
	} else {
		SLSI_INFO(sdev, "TId:%d ndp_ins_id:%d ndlVif:%d iface:%s\n",
			  hal_req->transaction_id, ndp_instance_id, ndl_vif_id, hal_req->ndp_iface);
		ndev_vif->nan.ndp_start_time = jiffies;
		ret = WIFI_HAL_SUCCESS;
	}

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_DP_INITIATOR_RESPONSE, (u16)ndp_instance_id, NULL, transaction_id, ndev_vif);
	kfree(hal_req);
	return ret;
}

int slsi_nan_ndp_respond_get_nl_param(struct slsi_dev *sdev, struct slsi_hal_nan_data_path_indication_response *hal_req,
				      const void *data, int len)
{
	int type, tmp, r;
	const struct nlattr *iter;
	u8 val = 0;
	u32 value = 0;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_NDP_INSTANCE_ID:
			slsi_util_nla_get_u32(iter, &hal_req->ndp_instance_id);
			break;

		case NAN_REQ_ATTR_DATA_INTERFACE_NAME:
			slsi_util_nla_get_data(iter, IFNAMSIZ, hal_req->ndp_iface);
			break;
		case NAN_REQ_ATTR_DATA_INTERFACE_NAME_LEN:
			break;

		case NAN_REQ_ATTR_SDEA_PARAM_SECURITY_CFG:
			slsi_util_nla_get_u8(iter, &hal_req->ndp_cfg.security_cfg);
			break;

		case NAN_REQ_ATTR_SDEA_PARAM_QOS_CFG:
			slsi_util_nla_get_u8(iter, &hal_req->ndp_cfg.qos_cfg);
			break;

		case NAN_REQ_ATTR_APP_INFO_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->app_info.ndp_app_info_len);
			break;

		case NAN_REQ_ATTR_APP_INFO:
			if (hal_req->app_info.ndp_app_info_len > SLSI_HAL_NAN_DP_MAX_APP_INFO_LEN)
				return -EINVAL;
			slsi_util_nla_get_data(iter, hal_req->app_info.ndp_app_info_len,
					       hal_req->app_info.ndp_app_info);
			break;

		case NAN_REQ_ATTR_NDP_RESPONSE_CODE:
			slsi_util_nla_get_u8(iter, &val);
			hal_req->rsp_code = (u32)val;
			break;

		case NAN_REQ_ATTR_SERVICE_NAME_LEN:
			slsi_util_nla_get_u32(iter, &value);
			hal_req->service_name_len = (u8)value;
			break;

		case NAN_REQ_ATTR_SERVICE_NAME:
			slsi_util_nla_get_data(iter, hal_req->service_name_len, hal_req->service_name);
			break;

		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			break;

		default:
			r = slsi_nan_get_security_info_nl(sdev, &hal_req->key_info, iter, type);
			if (r)
				SLSI_INFO(sdev, "Unexpected NAN ndp attribute TYPE:%d\n", type);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_ndp_respond(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	struct slsi_hal_nan_data_path_indication_response *hal_req = NULL;
	int ret;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;
	u16 transaction_id = 0, ndp_instance_id = 0, local_ndp_instance_id;

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "Failed to alloc hal_req structure!!!\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = WIFI_HAL_ERROR_OUT_OF_MEMORY;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_ndp_respond_get_nl_param(sdev, hal_req, data, len);
	transaction_id = hal_req->transaction_id;
	ndp_instance_id = hal_req->ndp_instance_id;
	if (reply_status) {
		ret = WIFI_HAL_ERROR_INVALID_ARGS;
		goto exit;
	}

	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_locks;
	}

	if (hal_req->ndp_instance_id > 0 && hal_req->ndp_instance_id <= SLSI_NAN_MAX_NDP_INSTANCES) {
		local_ndp_instance_id = ndev_vif->nan.ndp_local_ndp_instance_id[hal_req->ndp_instance_id - 1];
	} else {
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		ret = WIFI_HAL_ERROR_UNKNOWN;
		goto exit_with_locks;
	}
	ret = slsi_mlme_ndp_response(sdev, dev, hal_req, local_ndp_instance_id);
	if (ret) {
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		ret = WIFI_HAL_ERROR_UNKNOWN;
	} else {
		SLSI_INFO(sdev, "transId:%d ndpId:%d iface:%s\n",
			  hal_req->transaction_id, hal_req->ndp_instance_id, hal_req->ndp_iface);
		ret = WIFI_HAL_SUCCESS;
	}

exit_with_locks:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_DP_RESPONDER_RESPONSE,
				      ndp_instance_id, NULL, transaction_id, ndev_vif);
	kfree(hal_req);
	return ret;
}

int slsi_nan_ndp_end_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_data_end *hal_req,
				   const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;
	u32 val = 0;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		if (type == NAN_REQ_ATTR_NDP_INSTANCE_ID && hal_req->num_ndp_instances < SLSI_NAN_MAX_NDP_INSTANCES) {
			slsi_util_nla_get_u32(iter, &val);
			hal_req->ndp_instance_id[hal_req->num_ndp_instances++] = val;
		} else if (type == NAN_REQ_ATTR_HAL_TRANSACTION_ID) {
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_ndp_end(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	struct slsi_hal_nan_data_end hal_req;
	int ret, i;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	hal_req.transaction_id = 0;
	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS)
		goto exit;

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_ndp_end_get_nl_params(sdev, &hal_req, data, len);

	if (reply_status) {
		ret = WIFI_HAL_ERROR_INVALID_ARGS;
		goto exit;
	}

	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		slsi_nan_ndp_termination_handler(sdev, dev, 0, NULL);
		ret = WIFI_HAL_SUCCESS;
		goto exit_with_locks;
	}
	for (i = 0; i < hal_req.num_ndp_instances; i++)
		if (hal_req.ndp_instance_id[i] > 0 && hal_req.ndp_instance_id[i] <= SLSI_NAN_MAX_NDP_INSTANCES) {
			ret = slsi_mlme_ndp_terminate(sdev, dev, hal_req.ndp_instance_id[i], hal_req.transaction_id);
			SLSI_INFO(sdev,
				  "transId:%d ndp_instance_id:%d [%d/%d] mlme_terminate return:%d\n",
				  hal_req.transaction_id, hal_req.ndp_instance_id[i], i + 1, hal_req.num_ndp_instances, ret);
			if (!ret)
				ndev_vif->nan.ndp_state[hal_req.ndp_instance_id[i] - 1] = ndp_slot_status_terminating;
		} else {
			SLSI_ERR(sdev, "Ignore invalid ndp_instance_id:%d\n", hal_req.ndp_instance_id[i]);
		}

exit_with_locks:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_DP_END, 0, NULL, hal_req.transaction_id, ndev_vif);
	return ret;
}

/* NAN HAL EVENTS */

void slsi_nan_event(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct sk_buff *nl_skb = NULL;
	int res = 0;
	u16 event, identifier, evt_reason, followup_trans_id;
	u8 *mac_addr;
	u16 hal_event, reason_code, instanceid;
	struct netdev_vif *ndev_vif;
	enum slsi_nan_disc_event_type disc_event_type = 0;
	int i = 0;

	ndev_vif = netdev_priv(dev);
	event = fapi_get_u16(skb, u.mlme_nan_event_ind.event);
	identifier = fapi_get_u16(skb, u.mlme_nan_event_ind.identifier);
	mac_addr = fapi_get_buff(skb, u.mlme_nan_event_ind.address_or_identifier);
	reason_code = fapi_get_u16(skb, u.mlme_nan_event_ind.reason_code);
	instanceid = mac_addr[0] | (mac_addr[1] << 8);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	SLSI_INFO(sdev, "Event:%d, Identifier:%d, mac_addr:" MACSTR ", reason_code:%d\n",
		  event, identifier, MAC2STR(mac_addr), reason_code);
	switch (reason_code) {
	case FAPI_REASONCODE_NAN_SERVICE_TERMINATED_TIMEOUT:
	case FAPI_REASONCODE_NAN_SERVICE_TERMINATED_COUNT_REACHED:
	case FAPI_REASONCODE_NAN_SERVICE_TERMINATED_DISCOVERY_SHUTDOWN:
	case FAPI_REASONCODE_NAN_SERVICE_TERMINATED_USER_REQUEST:
	case FAPI_REASONCODE_NAN_TRANSMIT_FOLLOWUP_SUCCESS:
		evt_reason = SLSI_HAL_NAN_STATUS_SUCCESS;
		break;
	case FAPI_REASONCODE_NAN_TRANSMIT_FOLLOWUP_FAILURE:
		evt_reason = SLSI_HAL_NAN_STATUS_PROTOCOL_FAILURE;
		break;
	default:
		evt_reason = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		break;
	}

	switch (event) {
	case FAPI_EVENT_WIFI_EVENT_NAN_PUBLISH_TERMINATED:
	case FAPI_EVENT_WIFI_EVENT_NAN_SUBSCRIBE_TERMINATED:
		if (identifier > SLSI_NAN_MAX_SERVICE_ID) {
			SLSI_WARN(sdev, "serviceId(%d) > max(%d)\n", identifier, SLSI_NAN_MAX_SERVICE_ID);
			goto exit;
		}
		for (i = 0; i < BITS_TO_LONGS(SLSI_NAN_MAX_SERVICE_ID); i++)
			SLSI_DBG3(sdev, SLSI_GSCAN, "Service Id_Maps[%d]: 0x%x\n", i,
				  ndev_vif->nan.service_id_map[i]);
		SLSI_DBG3(sdev, SLSI_GSCAN, "sdf_flags:0x%x\n",
			  ndev_vif->nan.nan_sdf_flags[identifier]);
		if (!test_bit(identifier - 1, ndev_vif->nan.service_id_map))
			goto exit;
		clear_bit(identifier - 1, ndev_vif->nan.service_id_map);
		if (event == FAPI_EVENT_WIFI_EVENT_NAN_PUBLISH_TERMINATED) {
			if (ndev_vif->nan.nan_sdf_flags[identifier] & FAPI_NANSDFCONTROL_PUBLISH_END_EVENT) {
				ndev_vif->nan.nan_sdf_flags[identifier] = 0;
				goto exit;
			}
		} else {
			if (ndev_vif->nan.nan_sdf_flags[identifier] & FAPI_NANSDFCONTROL_SUBSCRIBE_END_EVENT) {
				ndev_vif->nan.nan_sdf_flags[identifier] = 0;
				goto exit;
			}
		}
		ndev_vif->nan.nan_sdf_flags[identifier] = 0;
		hal_event = event == FAPI_EVENT_WIFI_EVENT_NAN_PUBLISH_TERMINATED ?
			    SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT : SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_MATCH_EXPIRED:
		if (identifier > SLSI_NAN_MAX_SERVICE_ID) {
			SLSI_WARN(sdev, "serviceId(%d) > max(%d)\n", identifier, SLSI_NAN_MAX_SERVICE_ID);
			goto exit;
		}
		if (ndev_vif->nan.nan_sdf_flags[identifier] & FAPI_NANSDFCONTROL_MATCH_EXPIRED_EVENT)
			goto exit;
		hal_event = SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_ADDRESS_CHANGED:
		disc_event_type = NAN_EVENT_ID_DISC_MAC_ADDR;
		hal_event = SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT;
		ether_addr_copy(ndev_vif->nan.local_nmi, mac_addr);
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_CLUSTER_STARTED:
		disc_event_type = NAN_EVENT_ID_STARTED_CLUSTER;
		hal_event = SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT;
		ether_addr_copy(sdev->nan_cluster_id, mac_addr);
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_CLUSTER_JOINED:
		disc_event_type = NAN_EVENT_ID_JOINED_CLUSTER;
		hal_event = SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT;
		ether_addr_copy(sdev->nan_cluster_id, mac_addr);
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_TRANSMIT_FOLLOWUP:
		if (identifier > SLSI_NAN_MAX_SERVICE_ID) {
			SLSI_WARN(sdev, "serviceId(%d) > max(%d)\n", identifier, SLSI_NAN_MAX_SERVICE_ID);
			goto exit;
		}
		followup_trans_id = fapi_get_high16_u32(skb, u.mlme_nan_event_ind.spare_1);
		if (ndev_vif->nan.nan_sdf_flags[identifier] & FAPI_NANSDFCONTROL_FOLLOWUP_TRANSMIT_STATUS)
			goto exit;
		hal_event = SLSI_NL80211_NAN_TRANSMIT_FOLLOWUP_STATUS;
		SLSI_INFO(sdev, "followup_trans_id:%d\n", followup_trans_id);
		break;
	default:
		goto exit;
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_INFO(sdev, "Event: %s(%d)\n",
		  slsi_print_event_name(hal_event), hal_event);
#endif

#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, hal_event, GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, hal_event, GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		goto exit;
	}

	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_STATUS, evt_reason);
	switch (hal_event) {
	case SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PUBLISH_ID, identifier);
		break;
	case SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_MATCH_PUBLISH_SUBSCRIBE_ID, identifier);
		res |= nla_put_be32(nl_skb, NAN_EVT_ATTR_MATCH_REQUESTOR_INSTANCE_ID, instanceid);
		break;
	case SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_SUBSCRIBE_ID, identifier);
		break;
	case SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_DISCOVERY_ENGINE_EVT_TYPE, disc_event_type);
		res |= nla_put(nl_skb, NAN_EVT_ATTR_DISCOVERY_ENGINE_MAC_ADDR, ETH_ALEN, mac_addr);
		break;
	case SLSI_NL80211_NAN_TRANSMIT_FOLLOWUP_STATUS:
		res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_HAL_TRANSACTION_ID, followup_trans_id);
		break;
	}

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:0x%x\n", res);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		goto exit;
	}

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree_skb(skb);
}

void slsi_nan_send_disabled_event(struct slsi_dev *sdev, struct net_device *dev, u32 reason)
{
	struct sk_buff *nl_skb = NULL;
	int res = 0;
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_DISABLED_EVENT,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_DISABLED_EVENT,
					     GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		return;
	}
	res = nla_put_u32(nl_skb, NAN_EVT_ATTR_DISABLED_REASON, reason);
	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:0x%x\n", res);
		kfree_skb(nl_skb);
		return;
	}
	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
}

void slsi_nan_followup_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 tag_id, tag_len;
	u8  *ptr = NULL;
	struct slsi_hal_nan_followup_ind *hal_evt;
	struct sk_buff *nl_skb;
	int res;
	int sig_data_len;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	char *info_string = NULL;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	sig_data_len = fapi_get_datalen(skb);

	hal_evt = kmalloc(sizeof(*hal_evt), GFP_KERNEL);
	if (!hal_evt) {
		SLSI_ERR(sdev, "No memory for followup_ind\n");
		goto exit;
	}
	memset(hal_evt, 0, sizeof(*hal_evt));

	hal_evt->publish_subscribe_id = fapi_get_u16(skb, u.mlme_nan_followup_ind.session_id);
	hal_evt->requestor_instance_id = fapi_get_u16(skb, u.mlme_nan_followup_ind.match_id);
	ether_addr_copy(hal_evt->addr,
			fapi_get_buff(skb, u.mlme_nan_followup_ind.peer_nan_management_interface_address));

	SLSI_INFO(sdev, "pub_sub_id:%d, req_instance_id:%d, peer_addr:" MACSTR "\n",
		  hal_evt->publish_subscribe_id,
		  hal_evt->requestor_instance_id,
		  MAC2STR(hal_evt->addr));

	if (sig_data_len > 4)
		ptr = fapi_get_data(skb);

	if (ptr) {
		tag_id = le16_to_cpu(*(u16 *)ptr);
		tag_len = le16_to_cpu(*(u16 *)(ptr + 2));

		while (sig_data_len >= tag_len + 4) {
			if (tag_id == SLSI_NAN_TLV_TAG_SERVICE_SPECIFIC_INFO) {
				hal_evt->service_specific_info_len = tag_len > SLSI_HAL_NAN_MAX_SERVICE_SPECIFIC_INFO_LEN ?
							SLSI_HAL_NAN_MAX_SERVICE_SPECIFIC_INFO_LEN : tag_len;
				memcpy(hal_evt->service_specific_info, ptr + 4, hal_evt->service_specific_info_len);
				info_string = slsi_nan_convert_byte_to_string(hal_evt->service_specific_info_len,
									      hal_evt->service_specific_info);
				SLSI_DBG3(sdev, SLSI_GSCAN, "service_specific_info_len:%d, service_specific_info:%s\n",
					  hal_evt->service_specific_info_len, info_string);
			} else if (tag_id == SLSI_NAN_TLV_TAG_EXT_SERVICE_SPECIFIC_INFO) {
				if (tag_len > SLSI_HAL_NAN_MAX_SDEA_SERVICE_SPEC_INFO_LEN)
					hal_evt->sdea_service_specific_info_len = SLSI_HAL_NAN_MAX_SDEA_SERVICE_SPEC_INFO_LEN;
				else
					hal_evt->sdea_service_specific_info_len = tag_len;
				memcpy(hal_evt->sdea_service_specific_info, ptr + 4,
				       hal_evt->sdea_service_specific_info_len);
				info_string = slsi_nan_convert_byte_to_string(hal_evt->sdea_service_specific_info_len,
									      hal_evt->sdea_service_specific_info);
				SLSI_DBG3(sdev, SLSI_GSCAN,
					  "sdea_service_specific_info_len:%d, sdea_service_specific_info:%s\n",
					  hal_evt->sdea_service_specific_info_len, info_string);
			} else {
				SLSI_WARN(sdev, "Skip processing TLV %d\n", tag_id);
			}
			sig_data_len -= tag_len + 4;
			ptr += tag_len + 4;
			if (sig_data_len > 4) {
				tag_id = le16_to_cpu(*(u16 *)ptr);
				tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
			} else {
				tag_id = 0;
				tag_len = 0;
			}
		}
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_INFO(sdev, "Event: %s(%d)\n",
		  slsi_print_event_name(SLSI_NL80211_NAN_FOLLOWUP_EVENT), SLSI_NL80211_NAN_FOLLOWUP_EVENT);
#endif
#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_FOLLOWUP_EVENT,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_FOLLOWUP_EVENT,
					     GFP_KERNEL);
#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		kfree(hal_evt);
		goto exit;
	}

	res = nla_put_be16(nl_skb, NAN_EVT_ATTR_FOLLOWUP_PUBLISH_SUBSCRIBE_ID,
			   cpu_to_le16(hal_evt->publish_subscribe_id));
	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_FOLLOWUP_REQUESTOR_INSTANCE_ID,
			    cpu_to_le16(hal_evt->requestor_instance_id));
	res |= nla_put(nl_skb, NAN_EVT_ATTR_FOLLOWUP_ADDR, ETH_ALEN, hal_evt->addr);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_FOLLOWUP_DW_OR_FAW, hal_evt->dw_or_faw);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_FOLLOWUP_SERVICE_SPECIFIC_INFO_LEN, hal_evt->service_specific_info_len);
	if (hal_evt->service_specific_info_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_FOLLOWUP_SERVICE_SPECIFIC_INFO, hal_evt->service_specific_info_len,
			       hal_evt->service_specific_info);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_SDEA_LEN, hal_evt->sdea_service_specific_info_len);
	if (hal_evt->sdea_service_specific_info_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_SDEA, hal_evt->sdea_service_specific_info_len,
			       hal_evt->sdea_service_specific_info);

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:0x%x\n", res);
		kfree(hal_evt);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		goto exit;
	}

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
	kfree(hal_evt);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree_skb(skb);
}

#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
void slsi_nan_bootstrapping_requested_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct slsi_hal_nan_bootstrapping_requested_ind *hal_evt;
	struct sk_buff *nl_skb;
	int res;
	int sig_data_len;
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	sig_data_len = fapi_get_datalen(skb);

	hal_evt = kmalloc(sizeof(*hal_evt), GFP_KERNEL);
	if (!hal_evt) {
		SLSI_ERR(sdev, "No memory for bootstrapping_requested_ind\n");
		goto exit;
	}
	memset(hal_evt, 0, sizeof(*hal_evt));

	ether_addr_copy(hal_evt->peer_disc_mac_addr,
			fapi_get_buff(skb,
				      u.mlme_nan_bootstrapping_requested_ind.peer_nmi_address));
	hal_evt->requestor_instance_id = fapi_get_u16(skb, u.mlme_nan_bootstrapping_requested_ind.match_id);
	hal_evt->publish_subscribe_id = fapi_get_u16(skb, u.mlme_nan_bootstrapping_requested_ind.session_id);
	hal_evt->bootstrapping_method = fapi_get_u16(skb,
						     u.mlme_nan_bootstrapping_requested_ind.bootstrapping_method);
	hal_evt->bootstrapping_id = slsi_nan_get_new_bootstrapping_instance_id(ndev_vif);
	if (!hal_evt->bootstrapping_id) {
		kfree(hal_evt);
		goto exit;
	}

	SLSI_INFO(sdev, "pub_sub_id:%d, req_instance_id:%d, bootstrapping_instance_id:%d, peer_addr:" MACSTR "\n",
		  hal_evt->publish_subscribe_id,
		  hal_evt->requestor_instance_id,
		  hal_evt->bootstrapping_id,
		  MAC2STR(hal_evt->peer_disc_mac_addr));

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_INFO(sdev, "Event: %s(%d)\n",
		  slsi_print_event_name(SLSI_NL80211_NAN_BOOTSTRAPPING_REQ),
		  SLSI_NL80211_NAN_BOOTSTRAPPING_REQ);
#endif
#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE,
					     SLSI_NL80211_NAN_BOOTSTRAPPING_REQ,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE,
					     SLSI_NL80211_NAN_BOOTSTRAPPING_REQ,
					     GFP_KERNEL);
#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		kfree(hal_evt);
		goto exit;
	}

	res = nla_put_be16(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_PUBLISH_SUBSCRIBE_ID,
			   cpu_to_le16(hal_evt->publish_subscribe_id));
	res |= nla_put_be32(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_REQUESTOR_INSTANCE_ID,
			    cpu_to_le16(hal_evt->requestor_instance_id));
	res |= nla_put(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_ADDR, ETH_ALEN, hal_evt->peer_disc_mac_addr);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_METHOD, hal_evt->bootstrapping_method);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_INSTANCE_ID, hal_evt->bootstrapping_id);

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		kfree(hal_evt);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		goto exit;
	}

	set_bit(hal_evt->bootstrapping_id - 1, ndev_vif->nan.bootstrapping_instance_id_map);

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
	kfree(hal_evt);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree_skb(skb);
}

void slsi_nan_bootstrapping_confirm_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 tag_id, tag_len;
	u8  *ptr = NULL;
	const u8 *tag_data_ptr;
	struct slsi_hal_nan_bootstrapping_confirm_ind *hal_evt;
	struct sk_buff *nl_skb;
	int res;
	int sig_data_len;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	char *info_string = NULL;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	sig_data_len = fapi_get_datalen(skb);

	hal_evt = kmalloc(sizeof(*hal_evt), GFP_KERNEL);
	if (!hal_evt) {
		SLSI_ERR(sdev, "No memory for bootstrapping_confirm_ind\n");
		goto exit;
	}
	memset(hal_evt, 0, sizeof(*hal_evt));

	hal_evt->response = fapi_get_u16(skb, u.mlme_nan_bootstrapping_confirm_ind.response);
	/* Accept = 0, Reject = 1, Comeback = 2 */
	if (hal_evt->response == NAN_BOOTSTRAPPING_RESPONSE_REJECT)
		hal_evt->reason_code = SLSI_HAL_NAN_STATUS_PROTOCOL_FAILURE;
	else
		hal_evt->reason_code = SLSI_HAL_NAN_STATUS_SUCCESS;
	hal_evt->bootstrapping_id = fapi_get_u16(skb,
						 u.mlme_nan_bootstrapping_confirm_ind.bootstrapping_instance_id);

	SLSI_INFO(sdev, "response:%d, boot_instance_id:%d\n",
		  hal_evt->response,
		  hal_evt->bootstrapping_id);

	if (sig_data_len > 4)
		ptr = fapi_get_data(skb);

	if (ptr) {
		tag_id = le16_to_cpu(*(u16 *)ptr);
		tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
		tag_data_ptr = ptr + 4;

		while (sig_data_len >= tag_len + 4) {
			switch (tag_id) {
			case SLSI_NAN_TLV_NAN_BOOTSTRAPPING_COMEBACK_INFO:
				hal_evt->comebackinfo.comeback_delay = le32_to_cpu(*(u32 *)tag_data_ptr);
				tag_data_ptr += 4;
				hal_evt->comebackinfo.cookie_length = le16_to_cpu(*(u16 *)tag_data_ptr);
				tag_data_ptr += 2;
				memcpy(hal_evt->comebackinfo.cookie, tag_data_ptr, hal_evt->comebackinfo.cookie_length);
				info_string = slsi_nan_convert_byte_to_string(hal_evt->comebackinfo.cookie_length,
									      hal_evt->comebackinfo.cookie);
				tag_data_ptr += hal_evt->comebackinfo.cookie_length;
				SLSI_DBG3(sdev, SLSI_GSCAN, "cookie_len:%d, cookie:%s\n",
					  hal_evt->comebackinfo.cookie_length, info_string);
				break;
			default:
				SLSI_WARN(sdev, "Skip processing TLV %d\n", tag_id);
				break;
			}

			sig_data_len -= tag_len + 4;
			ptr += tag_len + 4;
			if (sig_data_len > 4) {
				tag_id = le16_to_cpu(*(u16 *)ptr);
				tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
				tag_data_ptr = ptr + 4;
			} else {
				tag_id = 0;
				tag_len = 0;
			}
		}
	}

	SLSI_DBG3(sdev, SLSI_GSCAN,
		  "response:%d,bootstrapping_instance_id:%d\n",
		  hal_evt->response, hal_evt->bootstrapping_id);

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_INFO(sdev, "Event: %s(%d)\n",
		  slsi_print_event_name(SLSI_NL80211_NAN_BOOTSTRAPPING_CFM),
		  SLSI_NL80211_NAN_BOOTSTRAPPING_CFM);
#endif
#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE,
					     SLSI_NL80211_NAN_BOOTSTRAPPING_CFM,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE,
					     SLSI_NL80211_NAN_BOOTSTRAPPING_CFM,
					     GFP_KERNEL);
#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		kfree(hal_evt);
		goto exit;
	}

	res = nla_put_u16(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_RSP_CODE, hal_evt->response);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_INSTANCE_ID, hal_evt->bootstrapping_id);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_REASON_CODE, hal_evt->reason_code);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_COME_BACK_DELAY,
			   hal_evt->comebackinfo.comeback_delay);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_COOKIE_LENGTH,
			   hal_evt->comebackinfo.cookie_length);
	if (hal_evt->comebackinfo.cookie_length)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_BOOTSTRAPPING_COOKIE,
			       hal_evt->comebackinfo.cookie_length, hal_evt->comebackinfo.cookie);

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		kfree(hal_evt);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		goto exit;
	}

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
	kfree(hal_evt);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree_skb(skb);
}

void slsi_nan_pairing_requested_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 tag_id, tag_len;
	u8  *ptr = NULL;
	const u8 *tag_data_ptr;
	struct slsi_hal_nan_pairing_requested_ind *hal_evt;
	struct sk_buff *nl_skb;
	int res;
	int sig_data_len;
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	sig_data_len = fapi_get_datalen(skb);

	hal_evt = kmalloc(sizeof(*hal_evt), GFP_KERNEL);
	if (!hal_evt) {
		SLSI_ERR(sdev, "No memory for followup_ind\n");
		goto exit;
	}
	memset(hal_evt, 0, sizeof(*hal_evt));

	hal_evt->publish_subscribe_id = fapi_get_u16(skb, u.mlme_nan_pairing_requested_ind.session_id);
	hal_evt->requestor_instance_id = fapi_get_u16(skb, u.mlme_nan_pairing_requested_ind.match_id);
	ether_addr_copy(hal_evt->peer_disc_mac_addr,
			fapi_get_buff(skb, u.mlme_nan_pairing_requested_ind.peer_nmi_address));
	hal_evt->pairing_request_type = fapi_get_u16(skb, u.mlme_nan_pairing_requested_ind.request_type);
	hal_evt->pairing_enable_pairing_cache = ndev_vif->nan.nan_enable_pairing_cache;
	hal_evt->pairing_instance_id = slsi_nan_get_new_pairing_instance_id(ndev_vif);
	if (!hal_evt->pairing_instance_id) {
		kfree(hal_evt);
		goto exit;
	} else {
		/* Store peer_mac|pairing_instance_id */
		ether_addr_copy(ndev_vif->nan.pairing_id_peer_mac[hal_evt->pairing_instance_id - 1],
				hal_evt->peer_disc_mac_addr);
	}

	/* Store mac_addr, match_id, session_id */
	ether_addr_copy(ndev_vif->nan.pairing_response_req.peer_disc_mac_addr, hal_evt->peer_disc_mac_addr);
	ndev_vif->nan.pairing_response_req.match_id = hal_evt->requestor_instance_id;
	ndev_vif->nan.pairing_response_req.session_id = hal_evt->publish_subscribe_id;

	/* Store nan_pairing_request_type */
	ndev_vif->nan.pairing_confirm_ind.nan_pairing_request_type = hal_evt->pairing_request_type;

	SLSI_INFO(sdev, "pub_sub_id:%d, req_instance_id:%d, req_type:%d, peer_addr: " MACSTR "\n",
		  hal_evt->publish_subscribe_id,
		  hal_evt->requestor_instance_id,
		  hal_evt->pairing_request_type,
		  MAC2STR(hal_evt->peer_disc_mac_addr));

	if (sig_data_len > 4)
		ptr = fapi_get_data(skb);

	if (ptr) {
		tag_id = le16_to_cpu(*(u16 *)ptr);
		tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
		tag_data_ptr = ptr + 4;

		while (sig_data_len >= tag_len + 4) {
			switch (tag_id) {
			case SLSI_NAN_TLV_NAN_IDENTITY_RESOLUTION_ATTR:
				if (tag_len < 0x0010) {
					SLSI_WARN(sdev, "Invalid taglen(%d) for SLSI_NAN_TLV_NAN_IDENTITY_RESOLUTION_ATTR\n",
						  tag_len);
					break;
				}
				memcpy(hal_evt->nik_resolution_attr.nonce, tag_data_ptr, NAN_IDENTITY_NONCE_LEN);
				tag_data_ptr += NAN_IDENTITY_NONCE_LEN;
				memcpy(hal_evt->nik_resolution_attr.tag, tag_data_ptr, NAN_IDENTITY_TAG_LEN);
				tag_data_ptr += NAN_IDENTITY_TAG_LEN;
				break;
			default:
				SLSI_WARN(sdev, "Skip processing TLV %d\n", tag_id);
				break;
			}
			sig_data_len -= tag_len + 4;
			ptr += tag_len + 4;
			if (sig_data_len > 4) {
				tag_id = le16_to_cpu(*(u16 *)ptr);
				tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
				tag_data_ptr = ptr + 4;
			} else {
				tag_id = 0;
				tag_len = 0;
			}
		}
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_INFO(sdev, "Event: %s(%d)\n",
		  slsi_print_event_name(SLSI_NL80211_NAN_PAIRING_REQ), SLSI_NL80211_NAN_PAIRING_REQ);
#endif
#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_PAIRING_REQ,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_PAIRING_REQ,
					     GFP_KERNEL);
#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		kfree(hal_evt);
		goto exit;
	}

	res = nla_put_be16(nl_skb, NAN_EVT_ATTR_PAIRING_PUBLISH_SUBSCRIBE_ID,
			   cpu_to_le16(hal_evt->publish_subscribe_id));
	res |= nla_put_be32(nl_skb, NAN_EVT_ATTR_PAIRING_REQUESTOR_INSTANCE_ID,
			    cpu_to_le16(hal_evt->requestor_instance_id));
	res |= nla_put(nl_skb, NAN_EVT_ATTR_PAIRING_PEER_DISC_MAC_ADDR, ETH_ALEN, hal_evt->peer_disc_mac_addr);
	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PAIRING_REQUEST_TYPE,
			    cpu_to_le16(hal_evt->pairing_request_type));
	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PAIRING_ENABLE_PAIRING_CACHE,
			    cpu_to_le16(hal_evt->pairing_enable_pairing_cache));
	res |= nla_put_be32(nl_skb, NAN_EVT_ATTR_PAIRING_PAIRING_INSTANCE_ID,
			    cpu_to_le16(hal_evt->pairing_instance_id));
	/* NIRA */
	res |= nla_put(nl_skb, NAN_EVT_ATTR_PAIRING_NIRA_NONCE, NAN_IDENTITY_NONCE_LEN,
		       hal_evt->nik_resolution_attr.nonce);
	res |= nla_put(nl_skb, NAN_EVT_ATTR_PAIRING_NIRA_TAG, NAN_IDENTITY_TAG_LEN,
		       hal_evt->nik_resolution_attr.tag);

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		kfree(hal_evt);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		goto exit;
	}

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
	kfree(hal_evt);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree_skb(skb);
}

void slsi_nan_pairing_request_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	ether_addr_copy(ndev_vif->nan.pairing_request_ind.peer_nmi_address,
			fapi_get_buff(skb, u.mlme_nan_pairing_request_ind.peer_nmi_address));
	ndev_vif->nan.pairing_request_ind.session_id = fapi_get_u16(skb, u.mlme_nan_pairing_request_ind.session_id);

	SLSI_INFO(sdev, "session_id:%d, peer_nmi_address: " MACSTR "\n",
		  ndev_vif->nan.pairing_request_ind.session_id,
		  MAC2STR(ndev_vif->nan.pairing_request_ind.peer_nmi_address));

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree_skb(skb);
}

void slsi_nan_pairing_response_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	ether_addr_copy(ndev_vif->nan.pairing_response_ind.peer_nmi_address,
			fapi_get_buff(skb, u.mlme_nan_pairing_response_ind.peer_nmi_address));
	ndev_vif->nan.pairing_response_ind.session_id = fapi_get_u16(skb, u.mlme_nan_pairing_response_ind.session_id);

	SLSI_INFO(sdev, "session_id:%d, peer_nmi_address: " MACSTR "\n",
		  ndev_vif->nan.pairing_response_ind.session_id,
		  MAC2STR(ndev_vif->nan.pairing_response_ind.peer_nmi_address));

	ndev_vif->nan.responder = 1;

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree_skb(skb);
}

u16 slsi_nan_pairing_get_instance_id_from_peer_nmi(struct net_device *dev, u8 *peer_nmi)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	int i = 0;

	for (i = 0; i < SLSI_NAN_MAX_PAIRING_INSTANCES; i++)
		if (ether_addr_equal(ndev_vif->nan.pairing_id_peer_mac[i], peer_nmi))
			return i + 1;
	return 0;
}

void slsi_nan_pairing_confirm_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 tag_id, tag_len;
	u8  *ptr = NULL;
	const u8 *tag_data_ptr;
	struct slsi_hal_nan_pairing_confirm_ind *hal_evt;
	struct sk_buff *nl_skb;
	int res;
	int sig_data_len;
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	sig_data_len = fapi_get_datalen(skb);

	hal_evt = kmalloc(sizeof(*hal_evt), GFP_KERNEL);
	if (!hal_evt) {
		SLSI_ERR(sdev, "No memory for followup_ind\n");
		goto exit;
	}
	memset(hal_evt, 0, sizeof(*hal_evt));

	ether_addr_copy(hal_evt->peer_nmi_address,
			fapi_get_buff(skb, u.mlme_nan_pairing_confirm_ind.peer_nmi_address));
	hal_evt->pairing_instance_id = slsi_nan_pairing_get_instance_id_from_peer_nmi(dev, hal_evt->peer_nmi_address);
	hal_evt->response = fapi_get_u16(skb, u.mlme_nan_pairing_confirm_ind.response);
	/* Accept = 0, Reject = 1 */
	if (hal_evt->response)
		hal_evt->reason_code = SLSI_HAL_NAN_STATUS_PROTOCOL_FAILURE;
	else
		hal_evt->reason_code = SLSI_HAL_NAN_STATUS_SUCCESS;

	hal_evt->pairing_request_type = ndev_vif->nan.pairing_confirm_ind.nan_pairing_request_type;
	hal_evt->pairing_enable_pairing_cache = ndev_vif->nan.nan_enable_pairing_cache;

	SLSI_INFO(sdev, "pairing_instance_id:%d, peer_addr: " MACSTR "\n",
		  hal_evt->pairing_instance_id,
		  MAC2STR(hal_evt->peer_nmi_address));

	if (sig_data_len > 4)
		ptr = fapi_get_data(skb);

	if (ptr) {
		tag_id = le16_to_cpu(*(u16 *)ptr);
		tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
		tag_data_ptr = ptr + 4;

		while (sig_data_len >= tag_len + 4) {
			switch (tag_id) {
			case SLSI_NAN_TLV_NAN_IDENTITY_KEY:
				hal_evt->islocalnik = le16_to_cpu(*(u16 *)tag_data_ptr);
				tag_data_ptr += 2;
				if (hal_evt->islocalnik) {
					memcpy(hal_evt->npk_security_association.local_nan_identity_key, tag_data_ptr,
					       SLSI_HAL_NAN_IDENTITY_KEY_LEN);
				} else {
					memcpy(hal_evt->npk_security_association.peer_nan_identity_key, tag_data_ptr,
					       SLSI_HAL_NAN_IDENTITY_KEY_LEN);
				}
				tag_data_ptr += SLSI_HAL_NAN_IDENTITY_KEY_LEN;
				break;
			default:
				SLSI_WARN(sdev, "Skip processing TLV %d\n", tag_id);
				break;
			}
			sig_data_len -= tag_len + 4;
			ptr += tag_len + 4;
			if (sig_data_len > 4) {
				tag_id = le16_to_cpu(*(u16 *)ptr);
				tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
				tag_data_ptr = ptr + 4;
			} else {
				tag_id = 0;
				tag_len = 0;
			}
		}
	}
#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_INFO(sdev, "Event: %s(%d)\n",
		  slsi_print_event_name(SLSI_NL80211_NAN_PAIRING_CFM), SLSI_NL80211_NAN_PAIRING_CFM);
#endif
#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_PAIRING_CFM,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_PAIRING_CFM,
					     GFP_KERNEL);
#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		kfree(hal_evt);
		goto exit;
	}

	res = nla_put_be32(nl_skb, NAN_EVT_ATTR_PAIRING_PAIRING_INSTANCE_ID,
			   cpu_to_le16(hal_evt->pairing_instance_id));
	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PAIRING_RSP_CODE,
			    cpu_to_le16(hal_evt->response));
	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PAIRING_REASON_CODE,
			    cpu_to_le16(hal_evt->reason_code));
	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PAIRING_REQUEST_TYPE,
			    cpu_to_le16(hal_evt->pairing_request_type));
	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PAIRING_ENABLE_PAIRING_CACHE,
			    cpu_to_le16(hal_evt->pairing_enable_pairing_cache));

	/* NPK Security Association */
	res |= nla_put(nl_skb, NAN_EVT_ATTR_PAIRING_PEER_NIK, SLSI_HAL_NAN_IDENTITY_KEY_LEN,
		       hal_evt->npk_security_association.peer_nan_identity_key);
	res |= nla_put(nl_skb, NAN_EVT_ATTR_PAIRING_LOCAL_NIK, SLSI_HAL_NAN_IDENTITY_KEY_LEN,
		       hal_evt->npk_security_association.local_nan_identity_key);

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		kfree(hal_evt);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		goto exit;
	}

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
	kfree(hal_evt);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree_skb(skb);
}

void slsi_nan_pasn_recv_mlme_ind(struct slsi_dev *sdev, struct net_device *dev)
{
	struct sk_buff *skb;
	int res = 0;

	SLSI_INFO(sdev, "[NAN][Pairing][Driver]slsi nan pasn recv mlme ind start!!\n");

#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE,
					  SLSI_NAN_PAIRING_PASN_RECV_MLME_INDICATION, GFP_KERNEL);
#else
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NAN_PAIRING_PASN_RECV_MLME_INDICATION,
					  GFP_KERNEL);
#endif
#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "[NAN][Pairing][Driver]Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NAN_PAIRING_PASN_RECV_MLME_INDICATION),
			SLSI_NAN_PAIRING_PASN_RECV_MLME_INDICATION);
#endif
	if (!skb) {
		SLSI_ERR(sdev, "[NAN][Pairing][Driver]NO MEM for skb!!!\n");
		return;
	}

	res = nla_put_u8(skb, NAN_EVT_ATTR_NAN_PAIRING_PASN_RECV_AUTH_TRANSACTION, sdev->pasn_recv.auth_transaction);
	res |= nla_put_u16(skb, NAN_EVT_ATTR_NAN_PAIRING_PASN_RECV_FRAME_LEN, sdev->pasn_recv.frame_len);
	res |= nla_put(skb, NAN_EVT_ATTR_NAN_PAIRING_PASN_RECV_FRAME, sdev->pasn_recv.frame_len, sdev->pasn_recv.frame);

	SLSI_ERR(sdev, "[NAN][Pairing][Driver]sdev->pasn_recv.auth_transaction : %d\n",
		 sdev->pasn_recv.auth_transaction);
	SLSI_ERR(sdev, "[NAN][Pairing][Driver]sdev->pasn_recv.frame_len : %d\n", sdev->pasn_recv.frame_len);
	SCSC_BIN_TAG_INFO(BINARY, sdev->pasn_recv.frame, sdev->pasn_recv.frame_len);

	if (res) {
		SLSI_ERR(sdev, "[NAN][Pairing][Driver]Error in nla_put*:%x\n", res);
		kfree_skb(skb);
		return;
	}
	cfg80211_vendor_event(skb, GFP_KERNEL);
}

int slsi_nan_pasn_set_key(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	const struct nlattr *iter;
	int type, tmp;
	int ret = 0;

	SLSI_INFO(sdev, "[NAN][Pairing][Driver]slsi nan pasn set key start!!\n");

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_PASN_SET_KEY_OWN_ADDR:
			slsi_util_nla_get_data(iter, ETH_ALEN, sdev->set_key_params.own_mac_addr);
			break;
		case NAN_PASN_SET_KEY_PEER_ADDR:
			slsi_util_nla_get_data(iter, ETH_ALEN, sdev->set_key_params.peer_mac_addr);
			break;
		case NAN_PASN_SET_KEY_NM_TK_LEN:
			slsi_util_nla_get_u8(iter, &sdev->set_key_params.nm_tk_len);
			break;
		case NAN_PASN_SET_KEY_NM_TK:
			slsi_util_nla_get_data(iter, sdev->set_key_params.nm_tk_len, sdev->set_key_params.nm_tk);
			break;
		case NAN_PASN_SET_KEY_NM_KEK_LEN:
			slsi_util_nla_get_u8(iter, &sdev->set_key_params.nm_kek_len);
			break;
		case NAN_PASN_SET_KEY_NM_KEK:
			slsi_util_nla_get_data(iter, sdev->set_key_params.nm_kek_len, sdev->set_key_params.nm_kek);
			break;
		case NAN_PASN_SET_KEY_CIPHER:
			slsi_util_nla_get_u32(iter, &sdev->set_key_params.cipher);
			break;
		default:
			SLSI_INFO(sdev, "[NAN][Pairing][Driver]Unexpected NAN_PASN attribute TYPE:%d\n", type);
		}
	}

	if (ndev_vif->nan.responder)
		ret = slsi_mlme_nan_pairing_response_response(sdev, dev, 0);
	else
		ret = slsi_mlme_nan_pairing_request_response(sdev, dev, 0);

	slsi_nan_pasn_send_key(sdev, dev);

	return ret;
}

void slsi_nan_pasn_send_key(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct key_params setkey_tk;
	struct key_params setkey_kek;
	int ret = 0;

	SLSI_INFO(sdev, "[NAN][Pairing][Driver]slsi nan pasn send key start!!\n");
	memset(&setkey_tk, 0, sizeof(setkey_tk));
	setkey_tk.key = sdev->set_key_params.nm_tk;
	setkey_tk.key_len = sdev->set_key_params.nm_tk_len;

	memset(&setkey_kek, 0, sizeof(setkey_kek));
	setkey_kek.key = sdev->set_key_params.nm_kek;
	setkey_kek.key_len = sdev->set_key_params.nm_kek_len;

	if (sdev->set_key_params.cipher == 16) {
		setkey_tk.cipher = 0x000fac04;
		setkey_kek.cipher = 0x000fac04;
	} else if (sdev->set_key_params.cipher == 256) {
		setkey_tk.cipher = 0x000fac09;
		setkey_kek.cipher = 0x000fac09;
	} else {
		SLSI_ERR(sdev, "[NAN][Pairing][Driver]Cipher type is not available\n");
	}

	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated) {
		SLSI_ERR(sdev, "NAN vif not activated\n");
	} else {
		ret = slsi_mlme_set_key(sdev, dev, 0, FAPI_KEYTYPE_PAIRWISE, sdev->set_key_params.peer_mac_addr,
					&setkey_tk);
		if (ret == FAPI_RESULTCODE_SUCCESS)
			SLSI_ERR(sdev, "[NAN][Pairing][Driver]FAPI_KEYTYPE_TK Success\n");
		else
			SLSI_ERR(sdev, "[NAN][Pairing][Driver]FAPI_KEYTYPE_TK Failed\n");

		ret = slsi_mlme_set_key(sdev, dev, 0, FAPI_KEYTYPE_KEK, sdev->set_key_params.peer_mac_addr,
					&setkey_kek);
		if (ret == FAPI_RESULTCODE_SUCCESS)
			SLSI_ERR(sdev, "[NAN][Pairing][Driver]FAPI_KEYTYPE_KEK Success\n");
		else
			SLSI_ERR(sdev, "[NAN][Pairing][Driver]FAPI_KEYTYPE_KEK Failed\n");
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}
#endif

void slsi_nan_service_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 tag_id, tag_len;
	u8  *ptr = NULL;
	const u8 *tag_data_ptr;
	int sig_data_len;
	struct slsi_hal_nan_match_ind *hal_evt;
	struct sk_buff *nl_skb;
	int res;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	char *info_string = NULL;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	sig_data_len = fapi_get_datalen(skb);
	if (sig_data_len <= 4) {
		SLSI_ERR(sdev, "Invalid data len(%d)\n", sig_data_len);
		goto exit;
	}

	hal_evt = kmalloc(sizeof(*hal_evt), GFP_KERNEL);
	if (!hal_evt) {
		SLSI_ERR(sdev, "No memory for service_ind\n");
		goto exit;
	}

	memset(hal_evt, 0, sizeof(*hal_evt));
	hal_evt->publish_subscribe_id = fapi_get_u16(skb, u.mlme_nan_service_ind.session_id);
	hal_evt->requestor_instance_id = fapi_get_u16(skb, u.mlme_nan_service_ind.match_id);

	SLSI_INFO(sdev, "pub_sub_id:%d, req_instance_id:%d, ranging_event:%d, range_measurement_mm:%d\n",
		  hal_evt->publish_subscribe_id, hal_evt->requestor_instance_id, hal_evt->ranging_event_type,
		  hal_evt->range_measurement_mm);

	if (sig_data_len > 4)
		ptr = fapi_get_data(skb);

	if (ptr) {
		tag_id = le16_to_cpu(*(u16 *)ptr);
		tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
		tag_data_ptr = ptr + 4;

		while (sig_data_len >= tag_len + 4) {
			switch (tag_id) {
			case SLSI_NAN_TLV_TAG_MATCH_IND:
				if (tag_len < 0x15) {
					SLSI_WARN(sdev, "Invalid taglen(%d) for SLSI_NAN_TLV_TAG_MATCH_IND\n", tag_len);
					break;
				}
				ether_addr_copy(hal_evt->addr, tag_data_ptr);
				/* To store the ethernet address for Cert */
				slsi_add_nan_discovery_info(ndev_vif, hal_evt->addr, hal_evt->publish_subscribe_id, hal_evt->requestor_instance_id);
				tag_data_ptr += ETH_ALEN;
				hal_evt->match_occurred_flag = le16_to_cpu(*(u16 *)tag_data_ptr);
				tag_data_ptr += 2;
				hal_evt->out_of_resource_flag = le16_to_cpu(*(u16 *)tag_data_ptr);
				tag_data_ptr += 2;
				hal_evt->rssi_value = *tag_data_ptr;
				tag_data_ptr++;
				hal_evt->sec_info.cipher_type = *tag_data_ptr;
				tag_data_ptr++;
				hal_evt->peer_sdea_params.security_cfg = le16_to_cpu(*(u16 *)tag_data_ptr);
				tag_data_ptr += 2;
				hal_evt->peer_sdea_params.ranging_state = le16_to_cpu(*(u16 *)tag_data_ptr);
				tag_data_ptr += 2;
				hal_evt->range_measurement_mm = le32_to_cpu(*(u32 *)tag_data_ptr) * 10;
				tag_data_ptr += 4;
				hal_evt->ranging_event_type = *tag_data_ptr;
				tag_data_ptr++;
				break;
			case SLSI_NAN_TLV_TAG_SERVICE_SPECIFIC_INFO:
				hal_evt->service_specific_info_len = tag_len > SLSI_HAL_NAN_MAX_SERVICE_SPECIFIC_INFO_LEN ?
							SLSI_HAL_NAN_MAX_SERVICE_SPECIFIC_INFO_LEN : tag_len;
				memcpy(hal_evt->service_specific_info, tag_data_ptr, hal_evt->service_specific_info_len);
				info_string = slsi_nan_convert_byte_to_string(hal_evt->service_specific_info_len,
									      hal_evt->service_specific_info);
				SLSI_DBG3(sdev, SLSI_GSCAN, "service_specific_info_len:%d, service_specific_info:%s\n",
					  hal_evt->service_specific_info_len, info_string);
				break;
			case SLSI_NAN_TLV_TAG_EXT_SERVICE_SPECIFIC_INFO:
				if (tag_len > SLSI_HAL_NAN_MAX_SDEA_SERVICE_SPEC_INFO_LEN)
					hal_evt->sdea_service_specific_info_len = SLSI_HAL_NAN_MAX_SDEA_SERVICE_SPEC_INFO_LEN;
				else
					hal_evt->sdea_service_specific_info_len = tag_len;
				memcpy(hal_evt->sdea_service_specific_info, tag_data_ptr,
				       hal_evt->sdea_service_specific_info_len);
				info_string = slsi_nan_convert_byte_to_string(hal_evt->sdea_service_specific_info_len,
									      hal_evt->sdea_service_specific_info);
				SLSI_DBG3(sdev, SLSI_GSCAN,
					  "sdea_service_specific_info_len:%d, sdea_service_specific_info:%s\n",
					  hal_evt->sdea_service_specific_info_len, info_string);
				break;
			case SLSI_NAN_TLV_TAG_DATA_PATH_SECURITY:
				if (tag_len < 7) {
					SLSI_WARN(sdev, "Invalid taglen(%d) for SLSI_NAN_TLV_TAG_DATA_PATH_SECURITY\n", tag_len);
					break;
				}
				hal_evt->sec_info.key_info.key_type = *tag_data_ptr;
				tag_data_ptr++;
				hal_evt->sec_info.cipher_type = *tag_data_ptr;
				tag_data_ptr++;
				break;
			case SLSI_NAN_TLV_TAG_MATCH_FILTER:
				if (tag_len > SLSI_HAL_NAN_MAX_MATCH_FILTER_LEN)
					hal_evt->sdf_match_filter_len = SLSI_HAL_NAN_MAX_MATCH_FILTER_LEN;
				else
					hal_evt->sdf_match_filter_len = tag_len;
				memcpy(hal_evt->sdf_match_filter, tag_data_ptr, hal_evt->sdf_match_filter_len);
				SLSI_DBG3(sdev, SLSI_GSCAN, "sdf_match_filter_len:%d, sdf_match_filter:%*.s\n",
					  hal_evt->sdf_match_filter_len, hal_evt->sdf_match_filter_len > 20 ? 20
					  : hal_evt->sdf_match_filter_len, hal_evt->sdf_match_filter);
				break;
#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
			case SLSI_NAN_TLV_NAN_PAIRING_CONFIG:
				if (tag_len < SLSI_HAL_NAN_PAIRING_CONFIG_LEN) {
					SLSI_WARN(sdev, "Invalid taglen(%d) for SLSI_NAN_TLV_NAN_PAIRING_CONFIG\n", tag_len);
					break;
				}
				hal_evt->pairing_cfg.enable_pairing_setup = le16_to_cpu(*(u16 *)tag_data_ptr);
				tag_data_ptr += 2;
				hal_evt->pairing_cfg.enable_pairing_cache = le16_to_cpu(*(u16 *)tag_data_ptr);
				tag_data_ptr += 2;
				hal_evt->pairing_cfg.enable_pairing_verification = le16_to_cpu(*(u16 *)tag_data_ptr);
				tag_data_ptr += 2;
				hal_evt->pairing_cfg.supported_bootstrapping_methods = le16_to_cpu(*(u16 *)tag_data_ptr);
				tag_data_ptr += 2;
				break;
			case SLSI_NAN_TLV_NAN_IDENTITY_RESOLUTION_ATTR:
				if (tag_len < 0x0010) {
					SLSI_WARN(sdev, "Invalid taglen(%d) for SLSI_NAN_TLV_NAN_IDENTITY_RESOLUTION_ATTR\n",
						  tag_len);
					break;
				}
				memcpy(hal_evt->nik_resolution_attr.nonce, tag_data_ptr, NAN_IDENTITY_NONCE_LEN);
				tag_data_ptr += NAN_IDENTITY_NONCE_LEN;
				memcpy(hal_evt->nik_resolution_attr.tag, tag_data_ptr, NAN_IDENTITY_TAG_LEN);
				tag_data_ptr += NAN_IDENTITY_TAG_LEN;
				break;
#endif
			default:
				SLSI_WARN(sdev, "Skip processing TLV %d\n", tag_id);
				break;
			}
			sig_data_len -= tag_len + 4;
			ptr += tag_len + 4;
			if (sig_data_len > 4) {
				tag_id = le16_to_cpu(*(u16 *)ptr);
				tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
				tag_data_ptr = ptr + 4;
			} else {
				tag_id = 0;
				tag_len = 0;
			}
		}
	}
	SLSI_DBG3(sdev, SLSI_GSCAN,
		  "match_addr:" MACSTR ",match_occurred_flag:%d,out_of_resource_flag:%d,rssi_value:%d,cipher_type:%d\n",
		  MAC2STR(hal_evt->addr), hal_evt->match_occurred_flag, hal_evt->out_of_resource_flag,
		  hal_evt->rssi_value,
		  hal_evt->sec_info.cipher_type);

#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
	SLSI_DBG3(sdev, SLSI_GSCAN,
		  "enable_pairing_setup:%d, enable_pairing_cache:%d, enable_pairing_verification:%d,"
		  "bootstrapping_method:%0x%04x, cipher_type:0x%04x\n",
		  hal_evt->pairing_cfg.enable_pairing_setup, hal_evt->pairing_cfg.enable_pairing_cache,
		  hal_evt->pairing_cfg.enable_pairing_verification,
		  hal_evt->pairing_cfg.supported_bootstrapping_methods, hal_evt->sec_info.cipher_type);
#endif
#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_INFO(sdev, "Event: %s(%d)\n",
		  slsi_print_event_name(SLSI_NL80211_NAN_MATCH_EVENT), SLSI_NL80211_NAN_MATCH_EVENT);
#endif
#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_MATCH_EVENT,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_MATCH_EVENT, GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		kfree(hal_evt);
		goto exit;
	}
	res = nla_put_u16(nl_skb, NAN_EVT_ATTR_MATCH_PUBLISH_SUBSCRIBE_ID, hal_evt->publish_subscribe_id);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_MATCH_REQUESTOR_INSTANCE_ID, hal_evt->requestor_instance_id);
	res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_ADDR, ETH_ALEN, hal_evt->addr);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_MATCH_SERVICE_SPECIFIC_INFO_LEN, hal_evt->service_specific_info_len);
	if (hal_evt->service_specific_info_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_SERVICE_SPECIFIC_INFO, hal_evt->service_specific_info_len,
			       hal_evt->service_specific_info);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_MATCH_SDF_MATCH_FILTER_LEN, hal_evt->sdf_match_filter_len);
	if (hal_evt->sdf_match_filter_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_SDF_MATCH_FILTER, hal_evt->sdf_match_filter_len,
			       hal_evt->sdf_match_filter);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_SDEA_LEN, hal_evt->sdea_service_specific_info_len);
	if (hal_evt->sdea_service_specific_info_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_SDEA, hal_evt->sdea_service_specific_info_len,
			       hal_evt->sdea_service_specific_info);

	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_MATCH_OCCURRED_FLAG, hal_evt->match_occurred_flag);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_OUT_OF_RESOURCE_FLAG, hal_evt->out_of_resource_flag);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_RSSI_VALUE, hal_evt->rssi_value);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_SDEA_PARAM_SECURITY_CONFIG, hal_evt->peer_sdea_params.security_cfg);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_SDEA_PARAM_RANGE_STATE, hal_evt->peer_sdea_params.ranging_state);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_RANGE_MEASUREMENT_MM, hal_evt->range_measurement_mm);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_RANGEING_EVENT_TYPE, hal_evt->ranging_event_type);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_SECURITY_CIPHER_TYPE, hal_evt->sec_info.cipher_type);

#ifdef CONFIG_SCSC_WIFI_NAN_PAIRING
	/* Pairing Cfg */
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_PAIRING_CFG_ENABLE_PAIRING_SETUP,
			   hal_evt->pairing_cfg.enable_pairing_setup);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_PAIRING_CFG_ENABLE_PAIRING_CACHE,
			   hal_evt->pairing_cfg.enable_pairing_cache);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_PAIRING_CFG_ENABLE_PAIRING_VERIFICATION,
			   hal_evt->pairing_cfg.enable_pairing_verification);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_PAIRING_CFG_SUPPORTED_BOOTSTRAPPING_METHODS,
			   hal_evt->pairing_cfg.supported_bootstrapping_methods);

	/* NIRA */
	res |= nla_put(nl_skb, NAN_EVT_ATTR_PAIRING_NIRA_NONCE, NAN_IDENTITY_NONCE_LEN,
		       hal_evt->nik_resolution_attr.nonce);
	res |= nla_put(nl_skb, NAN_EVT_ATTR_PAIRING_NIRA_TAG, NAN_IDENTITY_TAG_LEN,
		       hal_evt->nik_resolution_attr.tag);
#endif

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:0x%x\n", res);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		kfree(hal_evt);
		goto exit;
	}

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
	kfree(hal_evt);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree_skb(skb);
}

static void slsi_nan_get_resp_status_code(u16 fapi_result_code, u16 *resp_code, u16 *status_code)
{
	switch (fapi_result_code) {
	case FAPI_RESULTCODE_SUCCESS:
		*resp_code = NAN_DP_REQUEST_ACCEPT;
		*status_code = SLSI_HAL_NAN_STATUS_SUCCESS;
		break;
	case FAPI_RESULTCODE_NDP_REJECTED:
		*resp_code = NAN_DP_REQUEST_REJECT;
		*status_code = SLSI_HAL_NAN_STATUS_SUCCESS;
		break;
	case FAPI_RESULTCODE_NAN_NO_OTA_ACK:
		*resp_code = NAN_DP_REQUEST_REJECT;
		*status_code = SLSI_HAL_NAN_STATUS_NO_OTA_ACK;
		break;
	case FAPI_RESULTCODE_NAN_INVALID_AVAILABILITY:
	case FAPI_RESULTCODE_NAN_IMMUTABLE_UNACCEPTABLE:
	case FAPI_RESULTCODE_NAN_REJECTED_SECURITY_POLICY:
	case FAPI_RESULTCODE_NDL_UNACCEPTABLE:
		*resp_code = NAN_DP_REQUEST_REJECT;
		*status_code = SLSI_HAL_NAN_STATUS_PROTOCOL_FAILURE;
		break;
	case FAPI_RESULTCODE_TRANSMISSION_FAILURE:
	default:
		*resp_code = NAN_DP_REQUEST_REJECT;
		*status_code = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
	}
}

u32 slsi_nan_get_ndp_from_ndl_local_ndi(struct net_device *dev, u16 ndl_vif_id, const u8 *local_ndi)
{
	int j;
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	for (j = 0; j < SLSI_NAN_MAX_NDP_INSTANCES; j++) {
		if (ndev_vif->nan.ndp_instance_id2ndl_vif[j] == ndl_vif_id) {
			if (ether_addr_equal(ndev_vif->nan.ndp_ndi[j], local_ndi))
				return j + 1;
		}
	}
	return SLSI_NAN_MAX_NDP_INSTANCES + 1;
}

static int slsi_nan_put_ndp_req_ind_params(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb,
					   struct sk_buff *nl_skb, u8 **peer_ndi, u16 *resp_code, u16 *ndp_instance_id)
{
	int res;
	u16 status_code;

	*peer_ndi = fapi_get_buff(skb, u.mlme_ndp_request_ind.peer_ndp_interface_address);
	slsi_nan_get_resp_status_code(fapi_get_u16(skb, u.mlme_ndp_request_ind.result_code), resp_code, &status_code);
	*ndp_instance_id = fapi_get_u16(skb, u.mlme_ndp_request_ind.ndp_instance_id);

	res = nla_put_u32(nl_skb, NAN_EVT_ATTR_NDP_INSTANCE_ID, *ndp_instance_id);
	res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_ADDR, ETH_ALEN, *peer_ndi);

	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_NDP_RSP_CODE, *resp_code);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_STATUS_CODE, status_code);
	return res;
}

static int slsi_nan_put_ndp_resp_ind_params(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb,
					    struct sk_buff *nl_skb, u8 **peer_ndi, u16 *resp_code, u16 *ndp_instance_id)
{
	int res;
	u16 status_code;

	*peer_ndi = fapi_get_buff(skb, u.mlme_ndp_response_ind.peer_ndp_interface_address);
	slsi_nan_get_resp_status_code(fapi_get_u16(skb, u.mlme_ndp_response_ind.result_code), resp_code, &status_code);
	*ndp_instance_id = fapi_get_u16(skb, u.mlme_ndp_response_ind.ndp_instance_id);

	res = nla_put_u32(nl_skb, NAN_EVT_ATTR_NDP_INSTANCE_ID, *ndp_instance_id);
	res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_ADDR, ETH_ALEN, *peer_ndi);

	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_NDP_RSP_CODE, *resp_code);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_STATUS_CODE, status_code);

	return res;
}

void slsi_nan_ndp_setup_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb, bool is_req_ind)
{
	u16 tag_id, tag_len;
	u8  *ptr = NULL;
	const u8 *tag_data_ptr;
	int sig_data_len;
	struct sk_buff *nl_skb;
	int res;
	u8 *peer_ndi;
	u16 ndp_setup_response, ndp_instance_id;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	unsigned long delay_in_ms = 0;
	unsigned long ndp_setup_time = 0;
	char *info_string = NULL;
	u16 flow_id = 0;
	u16 ndl_vif_id = 0, ndl_id = 0;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	sig_data_len = fapi_get_datalen(skb);

#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NDP_CFM_EVENT,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NDP_CFM_EVENT, GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		goto exit;
	}

	if (is_req_ind) {
		res = slsi_nan_put_ndp_req_ind_params(sdev, dev, skb, nl_skb, &peer_ndi, &ndp_setup_response, &ndp_instance_id);
		flow_id = fapi_get_u16(skb, u.mlme_ndp_request_ind.flow_id);
	} else {
		res = slsi_nan_put_ndp_resp_ind_params(sdev, dev, skb, nl_skb, &peer_ndi, &ndp_setup_response, &ndp_instance_id);
		flow_id = fapi_get_u16(skb, u.mlme_ndp_response_ind.flow_id);
	}

	if (ndp_instance_id == 0 || ndp_instance_id > SLSI_NAN_MAX_NDP_INSTANCES) {
		SLSI_ERR(sdev, "Invalid ndp_instance_id:%d\n", ndp_instance_id);
		kfree_skb(nl_skb);
		goto exit;
	}

	SLSI_INFO(sdev, "is_req_ind:%d, peer_ndi:" MACSTR ", setup_response:%d, instance_id:%d\n",
		  is_req_ind, MAC2STR(peer_ndi), ndp_setup_response, ndp_instance_id);
	if (ndp_setup_response != NAN_DP_REQUEST_ACCEPT) {
		struct net_device *data_dev;
		struct netdev_vif *ndev_data_vif;

		data_dev = slsi_get_netdev_by_mac_addr_locked(sdev, ndev_vif->nan.ndp_ndi[ndp_instance_id - 1],
							      SLSI_NAN_DATA_IFINDEX_START);
		if (data_dev) {
			ndev_data_vif = netdev_priv(data_dev);
			SLSI_MUTEX_LOCK(ndev_data_vif->vif_mutex);
			ndev_data_vif->nan.ndp_count--;
			SLSI_MUTEX_UNLOCK(ndev_data_vif->vif_mutex);
		}
		slsi_nan_ndp_del_entry(sdev, dev, ndp_instance_id, false);
	}

	if (sig_data_len > 4)
		ptr = fapi_get_data(skb);

	if (ptr) {
		tag_id = le16_to_cpu(*(u16 *)ptr);
		tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
		tag_data_ptr = ptr + 4;
		while (sig_data_len >= tag_len + 4) {
			if (tag_id == SLSI_NAN_TLV_TAG_APP_INFO) {
				res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_APP_INFO_LEN, tag_len);
				res |= nla_put(nl_skb, NAN_EVT_ATTR_APP_INFO, tag_len, tag_data_ptr);
				info_string = slsi_nan_convert_byte_to_string(tag_len, (u8 *)tag_data_ptr);
				SLSI_DBG3(sdev, SLSI_GSCAN, "app_info_len:%d, app_info:%s\n", tag_len, info_string);
				break;
			} else if (tag_id == SLSI_NAN_TLV_WFA_SERVICE_INFO) {
				res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_APP_INFO_LEN, tag_len + 3);/* Tag 1 Bytes Length 2 bytes */
				ptr[1] = ptr[0];
				res |= nla_put(nl_skb, NAN_EVT_ATTR_APP_INFO, tag_len + 3, &ptr[1]);
				info_string = slsi_nan_convert_byte_to_string(tag_len + 3, &ptr[1]);
				SLSI_DBG3(sdev, SLSI_GSCAN, "service_app_info_len:%d, app_info:%s\n", tag_len + 3,
					  info_string);
				break;
			}
			sig_data_len -= tag_len + 4;
			ptr += tag_len + 4;
			if (sig_data_len > 4) {
				tag_id = le16_to_cpu(*(u16 *)ptr);
				tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
				tag_data_ptr = ptr + 4;
			} else {
				tag_id = 0;
				tag_len = 0;
			}
		}
	}

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:0x%x\n", res);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		goto exit;
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_INFO(sdev, "Event: %s(%d)\n",
		  slsi_print_event_name(SLSI_NL80211_NDP_CFM_EVENT), SLSI_NL80211_NDP_CFM_EVENT);
#endif
	if (ndp_setup_response == NAN_DP_REQUEST_ACCEPT) {
		struct netdev_vif *ndev_data_vif;
		struct net_device *data_dev;
		struct slsi_peer *peer = NULL;

		ndev_vif->nan.ndp_active_id_map |=  BIT(ndp_instance_id);
		data_dev = slsi_get_netdev_by_mac_addr_locked(sdev, ndev_vif->nan.ndp_ndi[ndp_instance_id - 1],
							      SLSI_NAN_DATA_IFINDEX_START);

		if (!data_dev) {
			SLSI_ERR(sdev, "no data_dev for ndp_instance_id:%d ndi[" MACSTR "]\n", ndp_instance_id, MAC2STR(ndev_vif->nan.ndp_ndi[ndp_instance_id - 1]));
			kfree_skb(nl_skb);
			goto exit;
		}
		ndev_data_vif = netdev_priv(data_dev);
		SLSI_MUTEX_LOCK(ndev_data_vif->vif_mutex);

		/* For NAN NDI VIF, the host does not create the VIF.
		 * But a successful MLME-NDP-REQUEST/RESPONSE.indication
		 * indicates the NDL VIF is successfully created/associated
		 * in Firmware. So "activated" is set to True here.
		 */

		ndev_data_vif->activated = true;

		slsi_spinlock_lock(&ndev_vif->peer_lock);
		peer = slsi_get_peer_from_mac(sdev, data_dev, peer_ndi);
		if (peer) {
			peer->ndp_count++;
		} else {
			ndl_vif_id = ndev_vif->nan.ndp_instance_id2ndl_vif[ndp_instance_id - 1];
			/* ndl_vif_id is subtracted with SLSI_NET_INDEX_NAN instead of SLSI_NAN_DATA_IFINDEX_START as we have to
			 *pass aid to peer add. aid will be subtracted by 1 to slsi_peer_add to derive the queueset
			 */
			ndl_id = ndl_vif_id - SLSI_NAN_MGMT_VIF_NUM(sdev);
			peer = slsi_peer_add(sdev, data_dev, peer_ndi, ndl_id);
			if (peer) {
				peer->connected_state = SLSI_STA_CONN_STATE_CONNECTED;
				slsi_ps_port_control(sdev, data_dev, peer, SLSI_STA_CONN_STATE_CONNECTED);
				peer->ndl_vif = ndl_vif_id;
				peer->qos_enabled = true;
			} else {
				SLSI_ERR(sdev, "no peer for ndp_instance_id:%d ndi[%d]\n", ndp_instance_id, ndev_vif->nan.ndp_ndi[ndp_instance_id - 1]);
			}
		}
		if (peer)
			peer->flow_id = flow_id;
		slsi_spinlock_unlock(&ndev_vif->peer_lock);

		if (ndev_data_vif->ifnum >= SLSI_NAN_DATA_IFINDEX_START) {
			dev->flags |= IFF_NOARP;
			netif_carrier_on(data_dev);
			/**
			 * ndp delay has been added due to NAN COMMS test failure in old android OS.
			 * Retest with android-U does not show any issue hence we disable delay to optimize
			 * NDP setup time.
			 */
			if (slsi_get_nan_ndp_delay() > 0 && slsi_get_nan_ndp_max_time() > 0 ) {
				ndp_setup_time = jiffies;
				if (ndp_setup_time > ndev_vif->nan.ndp_start_time)
					delay_in_ms = jiffies_to_msecs(ndp_setup_time - ndev_vif->nan.ndp_start_time);
				else
					delay_in_ms = jiffies_to_msecs(MAX_JIFFY_OFFSET - ndev_vif->nan.ndp_start_time + ndp_setup_time);
				if (delay_in_ms <  slsi_get_nan_ndp_delay())
					msleep(slsi_get_nan_ndp_max_time() - delay_in_ms);
			}
		}
		SLSI_MUTEX_UNLOCK(ndev_data_vif->vif_mutex);
	}
	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
exit:
	slsi_nan_dump_ndp_data(sdev, dev);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
	kfree_skb(skb);
}

void slsi_nan_ndp_requested_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 tag_id, tag_len = 0, ndl_vif_id, local_ndp_instance_id;
	u8  *ptr = NULL, *peer_nmi;
	const u8 *tag_data_ptr;
	int sig_data_len, res;
	struct sk_buff *nl_skb;
	u32 ndp_instance_id;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	char *info_string = NULL;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	sig_data_len = fapi_get_datalen(skb);

	ndev_vif->nan.ndp_start_time = jiffies;

#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NDP_REQ_EVENT,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NDP_REQ_EVENT, GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		goto exit;
	}

	local_ndp_instance_id = fapi_get_u16(skb, u.mlme_ndp_requested_ind.request_id);
	ndp_instance_id = slsi_nan_get_new_ndp_instance_id(ndev_vif);
	ndev_vif->nan.next_ndp_instance_id = (ndp_instance_id + 1) % (SLSI_NAN_MAX_NDP_INSTANCES + 1);
	peer_nmi = fapi_get_buff(skb, u.mlme_ndp_requested_ind.peer_nan_management_interface_address);
	res = nla_put_u16(nl_skb, NAN_EVT_ATTR_SERVICE_INSTANCE_ID,
			  fapi_get_u16(skb, u.mlme_ndp_requested_ind.session_id));
	res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_ADDR, ETH_ALEN, peer_nmi);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_NDP_INSTANCE_ID, ndp_instance_id);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_SDEA_PARAM_SECURITY_CONFIG,
			   fapi_get_u16(skb, u.mlme_ndp_requested_ind.security_required));

	SLSI_INFO(sdev, "session_id:%d, peer_nmi:" MACSTR ", ndp_instance_id:%d, security_req:%d\n",
		  fapi_get_u16(skb, u.mlme_ndp_requested_ind.session_id),
		  MAC2STR(peer_nmi),
		  ndp_instance_id,
		  fapi_get_u16(skb, u.mlme_ndp_requested_ind.security_required));

	if (sig_data_len > 4)
		ptr = fapi_get_data(skb);

	if (ptr) {
		tag_id = le16_to_cpu(*(u16 *)ptr);
		tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
		tag_data_ptr = ptr + 4;

		while (sig_data_len >= tag_len + 4) {
			if (tag_id == SLSI_NAN_TLV_TAG_APP_INFO) {
				res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_APP_INFO_LEN, tag_len);
				res |= nla_put(nl_skb, NAN_EVT_ATTR_APP_INFO, tag_len, tag_data_ptr);
				info_string = slsi_nan_convert_byte_to_string(tag_len, (u8 *)tag_data_ptr);
				SLSI_DBG3(sdev, SLSI_GSCAN, "app_info_len:%d, app_info:%s\n", tag_len, info_string);
				break;
			}
			sig_data_len -= tag_len + 4;
			ptr += tag_len + 4;
			if (sig_data_len > 4) {
				tag_id = le16_to_cpu(*(u16 *)ptr);
				tag_len = le16_to_cpu(*(u16 *)(ptr + 2));
				tag_data_ptr = ptr + 4;
			} else {
				tag_id = 0;
				tag_len = 0;
			}
		}
	}

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:0x%x\n", res);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		goto exit;
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_INFO(sdev, "Event: %s(%d)\n",
		  slsi_print_event_name(SLSI_NL80211_NDP_REQ_EVENT), SLSI_NL80211_NDP_REQ_EVENT);
#endif
	ndl_vif_id = slsi_nan_ndp_get_ndl_vif_id(sdev, peer_nmi, ndev_vif->nan.ndl_list);
	if (slsi_nan_ndp_new_entry(sdev, dev, ndp_instance_id, ndl_vif_id, NULL, peer_nmi) == 0) {
		cfg80211_vendor_event(nl_skb, GFP_KERNEL);
		ndev_vif->nan.ndp_local_ndp_instance_id[ndp_instance_id - 1] = local_ndp_instance_id;
	} else {
		struct slsi_hal_nan_data_path_indication_response response_req;

		kfree_skb(nl_skb);
		SLSI_ERR(sdev, "invalid ndl_vifid:%d ndp_instance_id:%d\n", ndl_vif_id, ndp_instance_id);
		memset(&response_req, 0, sizeof(response_req));
		response_req.rsp_code = NAN_DP_REQUEST_REJECT;
		slsi_mlme_ndp_response(sdev, dev, &response_req, local_ndp_instance_id);
	}

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
	kfree_skb(skb);
}

void slsi_nan_del_peer(struct slsi_dev *sdev, struct net_device *dev, u8 *local_ndi, u16 ndp_instance_id)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct net_device *data_dev;
	struct netdev_vif *ndev_data_vif;
	struct slsi_peer *peer = NULL;
	u16 ndl_vif_id = 0, ndl_id = 0;
	u16 nan_data_vif_index_start = SLSI_NAN_DATA_VIF_NUM_START(sdev);

	WLBT_WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (!local_ndi) {
		SLSI_ERR(sdev, "Invalid local NDI in del peer\n");
		return;
	}

	data_dev = slsi_get_netdev_by_mac_addr_locked(sdev, local_ndi, SLSI_NAN_DATA_IFINDEX_START);
	if (!data_dev) {
		SLSI_ERR(sdev, "Invalid Data Dev\n");
		return;
	}

	if (ndp_instance_id == 0 || ndp_instance_id > SLSI_NAN_MAX_NDP_INSTANCES)
		return;

	ndev_data_vif = netdev_priv(data_dev);
	SLSI_MUTEX_LOCK(ndev_data_vif->vif_mutex);
	ndl_vif_id = ndev_vif->nan.ndp_instance_id2ndl_vif[ndp_instance_id - 1];
	ndl_id = ndl_vif_id - nan_data_vif_index_start;
	peer = ndev_data_vif->peer_sta_record[ndl_id];
	if (peer->valid) {
		peer->ndp_count--;
		if (peer->ndp_count == 0) {
			slsi_ps_port_control(sdev, data_dev, peer, SLSI_STA_CONN_STATE_DISCONNECTED);
			slsi_spinlock_lock(&ndev_data_vif->peer_lock);
			slsi_peer_remove(sdev, data_dev, peer);
			slsi_spinlock_unlock(&ndev_data_vif->peer_lock);
		}
	} else {
		SLSI_ERR(sdev, "no peer for ndp_instance_id:%d ndi[" MACSTR "]\n", ndp_instance_id,
			 MAC2STR(ndev_vif->nan.ndp_ndi[ndp_instance_id - 1]));
	}
	SLSI_MUTEX_UNLOCK(ndev_data_vif->vif_mutex);
}

void slsi_nan_ndp_termination_handler(struct slsi_dev *sdev, struct net_device *dev, u16 ndp_instance_id, u8 *ndi)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff *nl_skb;
	struct netdev_vif *ndev_data_vif = NULL;
	struct net_device *data_dev = NULL;

	if (ndp_instance_id)
		data_dev = slsi_get_netdev_by_mac_addr_locked(sdev, ndev_vif->nan.ndp_ndi[ndp_instance_id - 1],
							      SLSI_NAN_DATA_IFINDEX_START);
	if (data_dev) {
		ndev_data_vif = netdev_priv(data_dev);
		SLSI_MUTEX_LOCK(ndev_data_vif->vif_mutex);
		if (ndev_data_vif->nan.ndp_count > 0)
			ndev_data_vif->nan.ndp_count--;
		SLSI_MUTEX_UNLOCK(ndev_data_vif->vif_mutex);
	}
	if (ndev_vif->nan.ndp_active_id_map & BIT(ndp_instance_id))
		slsi_nan_del_peer(sdev, dev, ndi, ndp_instance_id);
	ndev_vif->nan.ndp_active_id_map &= ~BIT(ndp_instance_id);
	slsi_nan_ndp_del_entry(sdev, dev, ndp_instance_id, false);

#if (KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE)
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NDP_END_EVENT,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NDP_END_EVENT, GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		return;
	}

	if (nla_put_u32(nl_skb, NAN_EVT_ATTR_NDP_INSTANCE_ID, ndp_instance_id)) {
		SLSI_ERR(sdev, "Error in nla_put_u32\n");
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		return;
	}
	SLSI_INFO(sdev, "ndp_instance_id:%d\n", ndp_instance_id);
	slsi_nan_dump_ndp_data(sdev, dev);
#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_INFO(sdev, "Event: %s(%d)\n",
		  slsi_print_event_name(SLSI_NL80211_NDP_END_EVENT), SLSI_NL80211_NDP_END_EVENT);
#endif
	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
}

void slsi_nan_ndp_termination_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 ndp_instance_id;
	u16 reason;
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	ndp_instance_id = fapi_get_u16(skb, u.mlme_ndp_terminated_ind.ndp_instance_id);
	reason = fapi_get_u16(skb, u.mlme_ndp_terminated_ind.reason_code);

	SLSI_INFO(sdev, "ndp_id:%d, reason:%d\n", ndp_instance_id, reason);

	if (ndp_instance_id <= SLSI_NAN_MAX_NDP_INSTANCES)
		slsi_nan_ndp_termination_handler(sdev, dev, ndp_instance_id, ndev_vif->nan.ndp_ndi[ndp_instance_id - 1]);

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
	kfree_skb(skb);
}

int slsi_send_nan_range_config(struct slsi_dev *sdev, u8 count, struct slsi_rtt_config *nl_rtt_params, int rtt_id)
{
	struct net_device *nan_dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *nan_ndev_vif;
	int  r = 0;
	bool is_locked = false;

	if (!slsi_dev_nan_supported(sdev)) {
		SLSI_ERR(sdev, "NAN not supported(mib:%d)\n", sdev->nan_enabled);
		r = WIFI_HAL_ERROR_NOT_SUPPORTED;
		goto exit;
	}
	if (!nan_dev) {
		SLSI_ERR(sdev, "dev is NULL!!\n");
		r = -EINVAL;
		goto exit;
	}
	nan_ndev_vif = netdev_priv(nan_dev);
	SLSI_MUTEX_LOCK(nan_ndev_vif->vif_mutex);
	is_locked = true;
#ifndef SLSI_TEST_DEV
	if (!nan_ndev_vif->activated) {
		SLSI_ERR(sdev, "NAN vif not activated\n");
		r = -EINVAL;
		goto exit;
	}
#endif
	if (r == 0)
		r = slsi_mlme_nan_range_req(sdev, nan_dev, count, nl_rtt_params);
exit:
	if (r) {
		kfree(sdev->rtt_id_params[rtt_id - 1]);
		sdev->rtt_id_params[rtt_id - 1] = NULL;
		SLSI_ERR_NODEV("Failed to set nan range config\n");
	}
	if (is_locked)
		SLSI_MUTEX_UNLOCK(nan_ndev_vif->vif_mutex);
	return r;
}

int slsi_send_nan_range_cancel(struct slsi_dev *sdev)
{
	struct net_device *nan_dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *nan_ndev_vif;
	int r = -EINVAL;

	if (!slsi_dev_nan_supported(sdev)) {
		SLSI_ERR(sdev, "NAN not supported(mib:%d)\n", sdev->nan_enabled);
		return WIFI_HAL_ERROR_NOT_SUPPORTED;
	}
	if (!nan_dev) {
		SLSI_ERR(sdev, "dev is NULL!!\n");
		return r;
	}
	nan_ndev_vif = netdev_priv(nan_dev);
	SLSI_MUTEX_LOCK(nan_ndev_vif->vif_mutex);
#ifndef SLSI_TEST_DEV
	if (!nan_ndev_vif->activated) {
		SLSI_ERR(sdev, "NAN vif not activated\n");
		SLSI_MUTEX_UNLOCK(nan_ndev_vif->vif_mutex);
		return r;
	}
#endif
	r = slsi_mlme_nan_range_cancel_req(sdev, nan_dev);
	SLSI_MUTEX_UNLOCK(nan_ndev_vif->vif_mutex);
	return r;
}

static void slsi_nan_trigger_rtt_complete_event(struct slsi_dev *sdev, u16 request_id, u8 rtt_id)
{
#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NL80211_RTT_COMPLETE_EVENT), SLSI_NL80211_RTT_COMPLETE_EVENT);
#endif
	slsi_vendor_event(sdev, SLSI_NL80211_RTT_COMPLETE_EVENT, &request_id, sizeof(request_id));

	kfree(sdev->rtt_id_params[rtt_id - 1]);
	sdev->rtt_id_params[rtt_id - 1] = NULL;
}

void slsi_rx_nan_range_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u32 tm;
	int data_len = fapi_get_datalen(skb);
	u32 tmac = fapi_get_u32(skb, u.mlme_nan_range_ind.timestamp);
	u8 *ip_ptr;
	struct sk_buff *nl_skb;
	int res = 0;
	struct nlattr *nlattr_nested;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
	struct timespec64 ts;
#else
	struct timespec ts;
#endif
	u64 tkernel;
	u16 value;
	u32 temp_value;
	int length = 0;
	u8 rtt_id = 0;
	u16 request_id = 0;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	for (rtt_id = SLSI_MIN_RTT_ID; rtt_id <= SLSI_MAX_RTT_ID; rtt_id++)
		if (sdev->rtt_id_params[rtt_id - 1] && sdev->rtt_id_params[rtt_id - 1]->peer_type == SLSI_RTT_PEER_NAN)
			break;
	if (rtt_id > SLSI_MAX_RTT_ID) {
		kfree_skb(skb);
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
		return;
	}
	request_id = sdev->rtt_id_params[rtt_id - 1]->hal_request_id;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE,
					     SLSI_NL80211_RTT_RESULT_EVENT, GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_RTT_RESULT_EVENT,
					     GFP_KERNEL);
#endif
#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NL80211_RTT_RESULT_EVENT), SLSI_NL80211_RTT_RESULT_EVENT);
#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		goto exit;
	}

	res |= nla_put_u16(nl_skb, SLSI_RTT_ATTRIBUTE_TARGET_ID, request_id);
	res |= nla_put_u8(nl_skb, SLSI_RTT_ATTRIBUTE_RESULTS_PER_TARGET, 1);
	ip_ptr = fapi_get_data(skb);
	while (length + SLSI_NAN_TLV_NAN_RTT_RESULT_LEN + 4 <= data_len) {
		nlattr_nested = nla_nest_start(nl_skb, SLSI_RTT_ATTRIBUTE_RESULT);
		if (!nlattr_nested) {
			SLSI_ERR(sdev, "Error in nla_nest_start\n");
			/* Dont use slsi skb wrapper for this free */
			kfree_skb(nl_skb);
			goto exit;
		}

		value = SLSI_BUFF_LE_TO_U16(ip_ptr);
		ip_ptr += 2;

		if (value != SLSI_NAN_TLV_NAN_RTT_RESULT) {
			SLSI_ERR(sdev, "Invalid TLV tag:0x%x\n", value);
			/* Dont use slsi skb wrapper for this free */
			kfree_skb(nl_skb);
			goto exit;
		}

		value = SLSI_BUFF_LE_TO_U16(ip_ptr);
		ip_ptr += 2;

		if (value != SLSI_NAN_TLV_NAN_RTT_RESULT_LEN) {
			SLSI_ERR(sdev, "Invalid TLV len:%d\n", value);
			/* Dont use slsi skb wrapper for this free */
			kfree_skb(nl_skb);
			goto exit;
		}

		res |= nla_put(nl_skb, SLSI_RTT_EVENT_ATTR_ADDR, ETH_ALEN, ip_ptr);
		ip_ptr += 6;

		value = SLSI_BUFF_LE_TO_U16(ip_ptr);
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_STATUS, value);
		ip_ptr += 2;

		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_NUM_PER_BURST_PEER, *ip_ptr);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_MEASUREMENT_NUM, *ip_ptr++);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_SUCCESS_NUM, *ip_ptr++);
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_BURST_NUM, 0);

		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_RETRY_AFTER_DURATION, 0);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_TYPE, 2);

		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_RSSI, *ip_ptr++);

		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_RSSI_SPREAD, *ip_ptr++);

		temp_value = le32_to_cpu(*(__le32 *)ip_ptr);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RTT, temp_value);
		ip_ptr += 4;

		value = SLSI_BUFF_LE_TO_U16(ip_ptr);
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_RTT_SD, value);
		ip_ptr += 2;

		value = SLSI_BUFF_LE_TO_U16(ip_ptr);
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_RTT_SPREAD, value);
		ip_ptr += 2;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
		ts = ktime_to_timespec64(ktime_get_boottime());
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		ts = ktime_to_timespec(ktime_get_boottime());
#else
		get_monotonic_boottime(&ts);
#endif
		tkernel = (u64)TIMESPEC_TO_US(ts);
		temp_value = le32_to_cpu(*(__le32 *)ip_ptr);
		tm = temp_value;
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TIMESTAMP_US, tkernel - (tmac - tm));
		ip_ptr += 4;

		temp_value = le32_to_cpu(*(__le32 *)ip_ptr);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_DISTANCE_MM, temp_value);
		ip_ptr += 4;

		temp_value = le32_to_cpu(*(__le32 *)ip_ptr);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_DISTANCE_SD_MM, temp_value);
		ip_ptr += 4;

		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_BURST_DURATION_MSN, *ip_ptr++);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_NEGOTIATED_BURST_NUM, 0);
		nla_nest_end(nl_skb, nlattr_nested);
		length += SLSI_NAN_TLV_NAN_RTT_RESULT_LEN + 4;
	}
	if (length != data_len || data_len == 0) {
		SLSI_ERR(sdev, "Incorrect length bulk data length:%d\n", data_len);
		kfree_skb(nl_skb);
		goto exit;
	}
	SLSI_DBG_HEX(sdev, SLSI_GSCAN, fapi_get_data(skb), fapi_get_datalen(skb), "nan_range indication skb buffer:\n");
	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:0x%x\n", res);
		kfree_skb(nl_skb);
		goto exit;
	}
	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
exit:
	slsi_nan_trigger_rtt_complete_event(sdev, request_id, rtt_id);
	kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

/**
 * slsi_vendor_nan_set_command_reply - This function responds to the std+ nan set command request.
 *
 * @wiphy: Pointer to the wiphy structure.
 * @status: status for request.
 * @transaction_id: Transaction ID.
 * @hal_cfm: Pointer to the structure that contains the response data for the std+ nan set command.
 *
 * This function is related to std+ feature.
 * It is a function that sends std+ nan set command request data and transfers the data.
 * received in response from firmware to hal through sk_buff.
 *
 * This code is a function that creates a response to the vendor command using cfg80211.
 * It allocates a response skb, adds the necessary properties, and sends the response.
 * Finally, it frees the hal_cfm structure.
 *
 * Return : static void
 */
static void slsi_vendor_nan_set_command_reply(struct wiphy *wiphy, u32 status, u16 transaction_id,
					      struct slsi_hal_nan_vendor_cmd_resp_data *hal_cfm)
{
	struct sk_buff  *reply;
	int             ret = 0;

	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, NLMSG_DEFAULT_SIZE);
	if (!reply) {
		SLSI_WARN_NODEV("[STD+][DRIVER]SKB alloc failed for vendor_cmd reply\n");
		kfree(hal_cfm);
		return;
	}

	ret |= nla_put_u16(reply, NAN_REPLY_ATTR_HAL_TRANSACTION_ID, transaction_id);
	ret |= nla_put_u32(reply, NAN_REPLY_ATTR_STATUS_TYPE, status);
	if (hal_cfm) {
		if (hal_cfm->cmd_resp_len == 0) {
			ret |= nla_put_u16(reply, NAN_REPLY_ATTR_SET_COMMAND_RESP_LEN, hal_cfm->cmd_resp_len);
			SLSI_WARN_NODEV("[STD+][DRIVER]NAN_REPLY_ATTR_SET_COMMAND_RESP data is NULL\n");
		} else {
			ret |= nla_put_u16(reply, NAN_REPLY_ATTR_SET_COMMAND_RESP_LEN, hal_cfm->cmd_resp_len);
			ret |= nla_put(reply, NAN_REPLY_ATTR_SET_COMMAND_RESP,
				       hal_cfm->cmd_resp_len, hal_cfm->cmd_resp);
		}
	} else {
		SLSI_WARN_NODEV("[STD+][DRIVER]NAN_REPLY_ATTR_SET_COMMAND_RESP hal_cfm is NULL\n");
	}
	if (ret) {
		SLSI_ERR_NODEV("[STD+][DRIVER]Error in nla_put:0x%x\n", ret);
		kfree_skb(reply);
	} else if (cfg80211_vendor_cmd_reply(reply)) {
		SLSI_ERR_NODEV("[STD+][DRIVER]FAILED to reply nan coammnd\n");
	}
	kfree(hal_cfm);
}

/**
 * slsi_vendor_nan_set_command_nl_params - This function sets the parameters of the std+ nan set command request.
 *
 * @sdev: a structure containing information about the device associated with the driver.
 * @hal_req: a structure to store std+ command request data.
 * @data: data containing properties.
 * @len: the length of the data.
 *
 * This function is related to std+ feature.
 * It receives std+ nan set command request data from vendor_wifi_nan hal and parses the data.
 *
 * This code is a function that extracts specific attributes from the given data and stores them in the hal_req structure.
 * Depending on the type of the attribute, it extracts the appropriate value and stores it in the corresponding field.
 *
 * Return: If all attributes are processed correctly, it returns SLSI_HAL_NAN_STATUS_SUCCESS.
 * If the attribute type is unexpected, it raises an error and returns SLSI_HAL_NAN_STATUS_INVALID_PARAM.
 */
static int slsi_vendor_nan_set_command_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_vendor_cmd_data *hal_req,
					   const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_HAL_TRANSACTION_ID:
			slsi_util_nla_get_u16(iter, &hal_req->transaction_id);
			SLSI_INFO(sdev, "[STD+][DRIVER]TRANSACTION_ID:%d\n", hal_req->transaction_id);
			break;

		case NAN_REQ_ATTR_SET_COMMAND_LEN:
			slsi_util_nla_get_u16(iter, &hal_req->cmd_req_len);
			SLSI_INFO(sdev, "[STD+][DRIVER]CMD_LEN:%d\n", hal_req->cmd_req_len);
			break;

		case NAN_REQ_ATTR_SET_COMMAND:
			slsi_util_nla_get_data(iter, hal_req->cmd_req_len, hal_req->cmd_req);
			SLSI_INFO(sdev, "[STD+][DRIVER]CMD\n");
			SCSC_BIN_TAG_INFO(BIN_WIFI_PM, hal_req->cmd_req, hal_req->cmd_req_len);
			break;
		default:
			SLSI_ERR(sdev, "[STD+][DRIVER] Unexpected STD+ attribute TYPE:%d\n", type);
			return SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

/**
 * slsi_vendor_nan_set_command - This function processes std+ nan set command request data.
 *
 * @wiphy: Pointer to the wiphy structure.
 * @wdev: Pointer to the wireless_dev structure.
 * @data: Pointer to the std+ nan command data.
 * @len: Length of std+ nan command data.
 *
 * This function is related to std+ feature.
 * std+ nan set command request data is received from vendor_wifi_nan hal.
 * after parsing the data, transmit it to the firmware, parse the response data from the firmware, and reply to the vendor_wifi_nan hal.
 *
 * This code is the slsi_vendor_nan_set_command function, which handles the std+ nan set command request.
 * First, the required memory is allocated, and a pre-check is performed to check whether the NAN is activated.
 * It then obtains the vif_mutex, and forwards the NAN command request to the MLME.
 * Finally, receive and process the response, and return the result.
 *
 * Context: This function obtains the vif_mutex of ndev_vif using SLSI_MUTEX_LOCK.
 *          If it works normally, the lock is released using SLSI_MUTEX_UNLOCK.
 *          If an error occurs, go directly to exit_with_lock and unlock using SLI_MUTEX_UNLOCK.
 *
 * Return: If all processes are correctly, it returns 0.
 * If the processes is not correct, it returns another result value.
 */
int slsi_vendor_nan_set_command(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif = NULL;
	struct slsi_hal_nan_vendor_cmd_data *hal_req = NULL;
	struct slsi_hal_nan_vendor_cmd_resp_data *hal_cfm = NULL;
	struct slsi_hal_nan_vendor_prev_cmd_data_info *prev_cmd_data_info = NULL;

	int ret = 0;
	u32 reply_status;
	u16 transaction_id = 0;

	SLSI_INFO(sdev, "[STD+][DRIVER]slsi_vendor_nan_set_command start\n");
	hal_req = kmalloc(sizeof(*hal_req)+len, GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "[STD+][DRIVER]Failed to alloc hal_req structure!!!\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	slsi_nan_pre_check(sdev, dev, &ret, &reply_status);
	if (ret != WIFI_HAL_SUCCESS) {
		kfree(hal_req);
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_vendor_nan_set_command_nl_params(sdev, hal_req, data, len);
	transaction_id = hal_req->transaction_id;
	if (reply_status != SLSI_HAL_NAN_STATUS_SUCCESS) {
		kfree(hal_req);
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "[STD+][DRIVER]NAN vif not activated\n");
		/* Queue STD+ command */
		prev_cmd_data_info = kzalloc(sizeof(*prev_cmd_data_info)+len, GFP_KERNEL);
		if (!prev_cmd_data_info) {
			SLSI_ERR(sdev, "[STD+][DRIVER]Failed to alloc prev_cmd_data_info structure!!!\n");
			reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
			ret = -ENOMEM;
			goto exit_with_lock;
		}
		memcpy(prev_cmd_data_info->prev_cmd_req, data, len);
		list_add(&prev_cmd_data_info->list, &sdev->slsi_hal_nan_vendor_prev_cmd_data);
		SLSI_WARN(sdev, "[STD+][DRIVER]prev_cmd_data_info queuing\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (hal_req->cmd_req_len)
		SLSI_INFO_HEX(sdev, hal_req->cmd_req, hal_req->cmd_req_len, "cmd_req\n");
	ret = slsi_mlme_vendor_nan_set_command(sdev, dev, hal_req, &hal_cfm);
	if (ret) {
		SLSI_ERR(sdev, "[STD+][DRIVER]hal_req->cmd_req failed\n");
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		ret = WIFI_HAL_ERROR_UNKNOWN;
	}
exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(hal_req);
exit:
	SLSI_INFO(sdev, "[STD+][DRIVER]slsi_vendor_nan_command_reply start\n");
	slsi_vendor_nan_set_command_reply(wiphy, reply_status, transaction_id, hal_cfm);
	SLSI_INFO(sdev, "[STD+][DRIVER]slsi_vendor_nan_command_reply ret:%d\n", ret);
	return ret;
}

/**
 * slsi_vendor_nan_set_command_event_ind - This function handles std+ command event notifications.
 *
 * @sdev: Pointer to the slsi_dev structure.
 * @dev: Pointer to the net_device structure.
 * @skb: Pointer to the sk_buff structure including event notification data.
 *
 * This function is related to std+ feature.
 * If the std+ nan set command is received from the peer device,
 * the data is parsed from the firmware and transmitted to the vendor_wifi_nan hal.
 *
 * This code is the slsi_vendor_nan_set_command_event_ind function, which handles std+nan set command event notifications.
 * First, the vif_mutex is obtained, and the data length and data are extracted from the FAPI skb.
 * The extracted data is then used to create a new skb, which is then transmitted using cfg80211_vendor_event.
 * Finally, release the original skb, and release vif_mutex.
 *
 * Context: This function obtains the vif_mutex of ndev_vif using SLSI_MUTEX_LOCK.
 *          If it works normally, the lock is released using SLSI_MUTEX_UNLOCK.
 *          If an error occurs, go directly to exit_with_lock and unlock using SLI_MUTEX_UNLOCK.
 *
 * Return: Void
 */
void slsi_vendor_nan_set_command_event_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	int data_len = fapi_get_datalen(skb);
	u8 *ptr;
	struct sk_buff *nl_skb;
	int res = 0;
	SLSI_INFO(sdev, "[STD+][DRIVER]slsi_vendor_nan_set_command_event_ind start\n");
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE,
						     SLSI_NANSTDP_EVENT_INDICATION, GFP_KERNEL);
	#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NANSTDP_EVENT_INDICATION,
						     GFP_KERNEL);
	#endif
	#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "[STD+][DRIVER]Event: %s(%d)\n",
				slsi_print_event_name(SLSI_NANSTDP_EVENT_INDICATION), SLSI_NANSTDP_EVENT_INDICATION);
	#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "[STD+][DRIVER]NO MEM for nl_skb!!!\n");
		goto exit;
	}
	ptr = fapi_get_data(skb);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_SET_COMMAND_EVENT_IND_LEN, data_len);
	res |= nla_put(nl_skb, NAN_EVT_ATTR_SET_COMMAND_EVENT_IND, data_len, ptr);
	if (res) {
		SLSI_ERR(sdev, "[STD+][DRIVER]Error in nla_put*:0x%x\n", res);
		kfree_skb(nl_skb);
		goto exit;
	}
	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree_skb(skb);
}

void slsi_vendor_nan_prev_cmd_data_free(struct slsi_dev *sdev){
	struct slsi_hal_nan_vendor_prev_cmd_data_info *prev_cmd_data_info,  *tmp;

	list_for_each_entry_safe(prev_cmd_data_info, tmp, &sdev->slsi_hal_nan_vendor_prev_cmd_data, list) {
		list_del(&prev_cmd_data_info->list);
		kfree(prev_cmd_data_info);
	}
}
