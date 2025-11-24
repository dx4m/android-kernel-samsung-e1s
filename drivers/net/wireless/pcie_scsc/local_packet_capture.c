/****************************************************************************
 *
 * Copyright (c) 2023 - 2023 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#include "dev.h"
#include "debug.h"
#include "mib.h"
#include "mgt.h"
#include "mlme.h"
#include "local_packet_capture.h"
#include <linux/ieee80211.h>
#include <pcie_scsc/scsc_mx.h>

#define LPC_HASH_BITS 8
#define LPC_TABLE_SIZE BIT(LPC_HASH_BITS)

static unsigned int lpc_distance_guard = 4000;
module_param(lpc_distance_guard, uint, 0644);
MODULE_PARM_DESC(lpc_distance_guard, "The minimum gap between widx and ridx to prevent incorrect parsing");

static bool lpc_debug_mode = false;
module_param(lpc_debug_mode, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(lpc_debug_mode, "");


static u32 ampdu_agg_idx = 0;

struct lpc_skb_data {
	u32 lpc_tag;
	struct sk_buff *lpc_skb;
	struct list_head list;
};

struct slsi_lpc_struct {
	int enable;
	struct net_device *lpc_dev;
	struct net_device *sta_dev;
	bool is_mgmt_type;
	bool is_data_type;
	bool is_ctrl_type;
	struct work_struct lpc_work;

	/* protects queue - send packet queue */
	spinlock_t lpc_queue_lock;
	struct sk_buff_head queue;

	/* protects lpc_rx_ampdu_info_table */
	spinlock_t lpc_ampdu_info_lock;
	struct list_head lpc_rx_ampdu_info_table[LPC_TABLE_SIZE];

	/* protects lpc_tx_buffer_table*/
	spinlock_t lpc_tx_skb_buffer_lock;
	struct list_head lpc_tx_buffer_table[LPC_TABLE_SIZE];

	/* protects lpc_rx_buffer_table*/
	spinlock_t lpc_rx_skb_buffer_lock;
	struct list_head lpc_rx_buffer_table[LPC_TABLE_SIZE];
} slsi_lpc_manager;

static void slsi_lpc_work_func(struct work_struct *work)
{
	struct slsi_lpc_struct *slsi_lpc_manager_w = container_of(work, struct slsi_lpc_struct, lpc_work);
	struct sk_buff *skb;

	spin_lock_bh(&slsi_lpc_manager_w->lpc_queue_lock);
	if (skb_queue_empty(&slsi_lpc_manager_w->queue)) {
		spin_unlock_bh(&slsi_lpc_manager_w->lpc_queue_lock);
		return;
	}
	while (!skb_queue_empty(&slsi_lpc_manager_w->queue)) {
		skb = __skb_dequeue(&slsi_lpc_manager_w->queue);
		if (slsi_lpc_manager.lpc_dev) {
			skb_reset_mac_header(skb);
			skb->dev = slsi_lpc_manager.lpc_dev;
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			skb->pkt_type = PACKET_OTHERHOST;
			skb->protocol = htons(ETH_P_802_2);
			netif_rx(skb);
		}
	}
	spin_unlock_bh(&slsi_lpc_manager_w->lpc_queue_lock);
}

static bool slsi_lpc_filter_capture_packet_type(u16 fc)
{
	if ((slsi_lpc_manager.is_mgmt_type && ieee80211_is_mgmt(fc)) ||
	    (slsi_lpc_manager.is_data_type && ieee80211_is_data(fc)) ||
	    (slsi_lpc_manager.is_ctrl_type && ieee80211_is_ctl(fc)))
		return true;
	return false;
}

void slsi_lpc_clear_packet_data(struct list_head *packet_list)
{
	struct lpc_skb_data *lpc_data, *lpc_data_tmp;

	list_for_each_entry_safe(lpc_data, lpc_data_tmp, packet_list, list) {
		list_del(&lpc_data->list);
		kfree_skb(lpc_data->lpc_skb);
		kfree(lpc_data);
	}
}

static struct lpc_skb_data *slsi_lpc_find_packet_data(u32 lpc_tag, struct list_head *packet_list)
{
	struct lpc_skb_data *lpc_data, *lpc_data_tmp;

	list_for_each_entry_safe(lpc_data, lpc_data_tmp, packet_list, list) {
		if (lpc_data->lpc_tag == lpc_tag)
			return lpc_data;
	}
	return NULL;
}

static struct sk_buff *slsi_lpc_delete_packet_data(u32 lpc_tag, struct list_head *packet_list)
{
	struct lpc_skb_data *lpc_data, *lpc_data_tmp;
	struct sk_buff *ret_skb;

	list_for_each_entry_safe(lpc_data, lpc_data_tmp, packet_list, list) {
		if (lpc_data->lpc_tag == lpc_tag) {
			list_del(&lpc_data->list);
			ret_skb = lpc_data->lpc_skb;
			kfree(lpc_data);
			return ret_skb;
		}
	}
	return NULL;
}

static int slsi_lpc_get_key_rsc_offset_in_amsdu(struct sk_buff *skb)
{
	struct slsi_skb_cb   *skb_cb = (struct slsi_skb_cb *)skb->cb;
	struct ieee80211_hdr *hdr;
	int offset = 0;

	hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_is_data(hdr->frame_control)) {
		SLSI_DBG3_NODEV(SLSI_RX, "Ignore this packet. It is not data\n");
		return offset;
	}

	if (ieee80211_has_a4(hdr->frame_control)) {
		offset += sizeof(struct ieee80211_hdr);
		SLSI_DBG4_NODEV(SLSI_RX, "80211 header has 4addr offset:%d\n", offset);
	} else {
		offset += sizeof(struct ieee80211_hdr_3addr);
	}

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		offset += IEEE80211_QOS_CTL_LEN;
	}

	if (ieee80211_has_order(hdr->frame_control)) {
		offset += IEEE80211_HT_CTL_LEN;
		SLSI_DBG4_NODEV(SLSI_RX, "80211 header has HT_CTL offset:%d\n", offset);
	}

	if (skb_cb->is_ciphered) {
		offset += SLSI_EAPOL_KEY_RSC_LENGTH;
		SLSI_DBG4_NODEV(SLSI_RX, "key-RSC is contained offset:%d\n", offset);
	}
	return offset;
}

static int __slsi_lpc_add_packet_data_rx(u32 lpc_tag, struct sk_buff *skb, struct list_head *packet_list)
{
	struct sk_buff *c_skb = NULL;
	struct sk_buff *skb2 = NULL;
	struct sk_buff *t_skb = NULL;
	struct lpc_skb_data *new_lpc_data = NULL;
	struct slsi_skb_cb   *skb_cb = (struct slsi_skb_cb *)skb->cb;

	new_lpc_data = slsi_lpc_find_packet_data(lpc_tag, packet_list);
	if (new_lpc_data) {
		c_skb = slsi_lpc_delete_packet_data(lpc_tag, packet_list);
		if (c_skb)
			kfree_skb(c_skb);
	}

	new_lpc_data = kmalloc(sizeof(*new_lpc_data), GFP_ATOMIC);
	if (!new_lpc_data) {
		SLSI_ERR_NODEV("No memory for new_lpc_data\n");
		return -ENOMEM;
	}

	if (skb_has_frag_list(skb)) {
		struct sk_buff *frag_skb;
		unsigned int frag_len;
		skb2 = alloc_skb(skb->len + 4, GFP_ATOMIC);
		if (!skb2) {
			SLSI_ERR_NODEV("Error allocating skb2 %d bytes\n", skb->len);
			kfree(new_lpc_data);
			return -EINVAL;
		}
		frag_len = (unsigned char *)skb_tail_pointer(skb) - skb->data;
		if (frag_len)
			memcpy(skb_put(skb2, frag_len), skb->data, frag_len);

		skb_walk_frags(skb, frag_skb) {
			frag_len = (unsigned char *)skb_tail_pointer(frag_skb) - frag_skb->data;
			if (frag_len)
				memcpy(skb_put(skb2, frag_len), frag_skb->data, frag_len);
		}
		t_skb = skb2;
	} else {
		t_skb = skb;
	}

	if (skb_cb->is_ciphered) {
		int offset = slsi_lpc_get_key_rsc_offset_in_amsdu(skb);
		unsigned char *data_at_offset = t_skb->data + offset;

		if (offset < SLSI_EAPOL_KEY_RSC_LENGTH) {
			SLSI_WARN_NODEV("Wrong key_rsc offset.\n");
			kfree(new_lpc_data);
			if (skb2)
				kfree_skb(skb2);
			return -1;
		}

		c_skb = alloc_skb(LPC_RADIOTAP_MAX_LEN + t_skb->len + LPC_SKB_PADDING, GFP_ATOMIC);
		if (!c_skb) {
			SLSI_ERR_NODEV("Alloc skb Failed\n");
			kfree(new_lpc_data);
			if (skb2)
				kfree_skb(skb2);
			return -ENOMEM;
		}

		skb_reserve(c_skb, LPC_RADIOTAP_MAX_LEN);
		memcpy(skb_put(c_skb, offset - SLSI_EAPOL_KEY_RSC_LENGTH), t_skb->data, offset - SLSI_EAPOL_KEY_RSC_LENGTH);
		memcpy(skb_put(c_skb, t_skb->len - offset), data_at_offset, t_skb->len - offset);

		new_lpc_data->lpc_tag = lpc_tag;
		new_lpc_data->lpc_skb = c_skb;
		INIT_LIST_HEAD(&new_lpc_data->list);
		list_add_tail(&new_lpc_data->list, packet_list);
	} else {
		c_skb = skb_copy(t_skb, GFP_ATOMIC);
		new_lpc_data->lpc_tag = lpc_tag;
		new_lpc_data->lpc_skb = c_skb;
		INIT_LIST_HEAD(&new_lpc_data->list);
		list_add_tail(&new_lpc_data->list, packet_list);
	}
	if (skb2)
		kfree_skb(skb2);
	return 0;
}

static int __slsi_lpc_add_packet_data(u32 lpc_tag, struct sk_buff *skb, struct list_head *packet_list)
{
	struct sk_buff *c_skb;
	struct lpc_skb_data *new_lpc_data;

	new_lpc_data = slsi_lpc_find_packet_data(lpc_tag, packet_list);
	if (new_lpc_data) {
		c_skb = slsi_lpc_delete_packet_data(lpc_tag, packet_list);
		if (c_skb)
			kfree_skb(c_skb);
	}
	new_lpc_data = kmalloc(sizeof(*new_lpc_data), GFP_ATOMIC);
	if (!new_lpc_data) {
		SLSI_ERR_NODEV("No memory for new_lpc_data\n");
		return -ENOMEM;
	}
	c_skb = skb_copy(skb, GFP_ATOMIC);
	new_lpc_data->lpc_tag = lpc_tag;
	new_lpc_data->lpc_skb = c_skb;
	INIT_LIST_HEAD(&new_lpc_data->list);
	list_add_tail(&new_lpc_data->list, packet_list);

	return 0;
}

int slsi_lpc_add_packet_data(u32 lpc_tag, struct sk_buff *skb, int data_type)
{
	int ret = -EINVAL;

	if (!slsi_lpc_manager.enable)
		return 0;

	if (data_type == SLSI_LPC_DATA_TYPE_TX) {
		spin_lock_bh(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);
		ret = __slsi_lpc_add_packet_data(lpc_tag, skb, &slsi_lpc_manager.lpc_tx_buffer_table[lpc_tag & (LPC_TABLE_SIZE - 1)]);
		spin_unlock_bh(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);
	} else if (data_type == SLSI_LPC_DATA_TYPE_RX) {
		spin_lock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
		ret = __slsi_lpc_add_packet_data_rx(lpc_tag, skb, &slsi_lpc_manager.lpc_rx_buffer_table[lpc_tag & (LPC_TABLE_SIZE - 1)]);
		spin_unlock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
	} else if (data_type == SLSI_LPC_DATA_TYPE_AMPDU_RX_INFO) {
		spin_lock_bh(&slsi_lpc_manager.lpc_ampdu_info_lock);
		ret = __slsi_lpc_add_packet_data(lpc_tag, skb, &slsi_lpc_manager.lpc_rx_ampdu_info_table[lpc_tag & (LPC_TABLE_SIZE - 1)]);
		spin_unlock_bh(&slsi_lpc_manager.lpc_ampdu_info_lock);
	}

	return ret;
}

static int slsi_lpc_get_bits(int idx_to, int idx_from, u32 value)
{
	return (value >> idx_from) & ((1 << (idx_to - idx_from + 1)) - 1);
}

static int slsi_lpc_get_bit(int idx, u32 value)
{
	return (value >> idx) & 0x1;
}

static int slsi_lpc_wyrate_get_phy_rate(struct acc_mod_opt_decode *amod)
{
	int n_data_subcarriers = 0;
	int t_ofm_symbal_duration = 32; /* X10 */
	int t_gi = 0; /* X10 */
	int n_bpscs_r_by_mcs_subcarrier[12] = {1, 2, 2, 4, 4, 6, 6, 6, 8, 8, 10, 10};
	int n_bpscs_r_by_mcs_r[12] = {50, 50, 75, 50, 75, 67, 75, 83, 75, 83, 75, 83}; /* X100 */
	int n_coded_bits_per_subcarrier = (amod->wy_mcs >= 0 && amod->wy_mcs < 12) ? n_bpscs_r_by_mcs_subcarrier[amod->wy_mcs] : n_bpscs_r_by_mcs_subcarrier[11];
	int r = (amod->wy_mcs >= 0 && amod->wy_mcs < 12) ? n_bpscs_r_by_mcs_r[amod->wy_mcs] : n_bpscs_r_by_mcs_r[11];
	int mbps = 0;

	if (amod->wy_phy == WYRATE_PHY_TYPE_11AX) {
		int t_gi_map[3] = {8, 16, 32}; /* X10 */
		t_ofm_symbal_duration = 128; /* X10 */
		t_gi = (amod->wy_gi >= 0 && amod->wy_gi < 3) ? t_gi_map[amod->wy_gi] : t_gi_map[2]; /* X10 */

		if (amod->wy_ax_bw_idx_or_tone >= 0 && amod->wy_ax_bw_idx_or_tone < 10) {
			int n_data_subcarriers_by_bw_idx_or_tone[10] = {234, 468, 980, 160, 24, 48, 102, 234, 468, 980};
			n_data_subcarriers = n_data_subcarriers_by_bw_idx_or_tone[amod->wy_ax_bw_idx_or_tone];
		} else {
			int n_data_subcarriers_by_bw[9] = {0, 234, 468, 0, 980, 0, 0, 0, 1960}; /* 20:234, 40:468, 80:980, 160:1960 */
			n_data_subcarriers = n_data_subcarriers_by_bw[amod->wy_bw / 20];
		}
	} else {
		int t_gi_map[2] = {8, 14}; /* X10 */
		int n_data_subcarriers_by_bw[9] = {0, 52, 108, 0, 234, 0, 0, 0, 468}; /* 20:52, 40:108, 80:234, 160:468 */
		t_ofm_symbal_duration = 32; /* 3.2 X 10 */
		t_gi = (amod->wy_gi >= 0 && amod->wy_gi < 2) ? t_gi_map[amod->wy_gi] : t_gi_map[1]; /* X10 */
		n_data_subcarriers = n_data_subcarriers_by_bw[amod->wy_bw / 20];
	}
	mbps = (n_data_subcarriers * n_coded_bits_per_subcarrier * r * amod->wy_nss) / (t_ofm_symbal_duration + t_gi); /* X10 */

	return mbps;
}

static void slsi_lpc_accmod_opt_decode(u32 rate, struct acc_mod_opt_decode *amod)
{
	int rate_idx_with_nss = 0;
	int bw_map_by_mod_type[12] = {20, 20, 20, 40, 20, 40, 80, 160, 10, 40, 80, 160};
	int is_he = slsi_lpc_get_bit(16, rate);
	int wy_bw_arr_idx = 0; /* wy_bw / 20 */

	amod->wy_phy = 5;
	amod->wy_nss = 1;
	amod->wy_bw = 0;
	amod->wy_mcs = 0;
	amod->wy_gi = 1;
	amod->wy_stbc = 0;
	amod->wy_ldpc = 0;
	amod->wy_ax_bw_idx_or_tone = -1;
	amod->rate_idx_with_nss = 0;
	amod->phy_rate = 1;
	amod->short_preamble = 0;
	amod->duplicate_mode = 0;
	amod->acc_mode_type = rate;
	amod->mode_type = 0;
	amod->gf = 0;
	amod->is_valid = (rate & 0xffff) != 0xffff;

	if (!amod->is_valid)
		return;

	amod->mode_type = slsi_lpc_get_bits(11, 9, rate);
	if (amod->mode_type < 0 || amod->mode_type > 11) {
		amod->is_valid = 0;
		SLSI_WARN_NODEV("wrong amod->mode_type:%d\n", amod->mode_type);
		return;
	}
	amod->wy_bw = bw_map_by_mod_type[amod->mode_type];
	wy_bw_arr_idx = amod->wy_bw / 20;
	if (wy_bw_arr_idx < 0 || wy_bw_arr_idx > 8) {
		amod->is_valid = 0;
		SLSI_WARN_NODEV("wrong amod->wy_bw :%d\n", amod->wy_bw );
		return;
	}
	amod->wy_stbc = slsi_lpc_get_bit(8, rate);
	amod->wy_ldpc = slsi_lpc_get_bit(13, rate);

	switch(amod->mode_type) {
	case 0:
		amod->wy_phy = WYRATE_PHY_TYPE_11A;
		amod->wy_mcs = slsi_lpc_get_bits(2, 0, rate);
		amod->duplicate_mode = slsi_lpc_get_bits(6, 5, rate);
		break;
	case 1:
		amod->wy_phy = WYRATE_PHY_TYPE_11B;
		amod->wy_mcs = slsi_lpc_get_bits(1, 0, rate);
		amod->short_preamble = slsi_lpc_get_bit(4, rate);
		amod->duplicate_mode = slsi_lpc_get_bits(6, 5, rate);
		break;
	case 2:
	case 3:
		amod->wy_phy = WYRATE_PHY_TYPE_11N;
		amod->rate_idx_with_nss = slsi_lpc_get_bits(6, 0, rate);
		amod->wy_mcs = rate_idx_with_nss % 8;
		amod->wy_nss = 1 + rate_idx_with_nss / 8;
		amod->wy_gi = !(slsi_lpc_get_bit(12, rate));
		amod->gf = slsi_lpc_get_bit(7, rate);
		break;
	case 4:
	case 5:
	case 6:
	case 7:
		if (!is_he) {
			amod->wy_phy = WYRATE_PHY_TYPE_11AC;
			amod->wy_mcs = slsi_lpc_get_bits(3, 0, rate);
			amod->wy_nss = slsi_lpc_get_bits(6, 4, rate) + 1;
			amod->gf = slsi_lpc_get_bit(12, rate);
			amod->wy_gi = !(slsi_lpc_get_bit(12, rate));
			break;
		}
		amod->wy_phy = WYRATE_PHY_TYPE_11AX;
		amod->wy_mcs = slsi_lpc_get_bits(3, 0, rate);
		amod->wy_nss = slsi_lpc_get_bits(6, 4, rate) + 1;
		amod->wy_gi = slsi_lpc_get_bits(18, 17, rate);
		amod->wy_ax_bw_idx_or_tone = slsi_lpc_get_bits(31, 24, rate);
		break;
	case 8:
	case 9:
	case 10:
	case 11:
		amod->wy_phy = WYRATE_PHY_TYPE_11AX;
		amod->wy_mcs = slsi_lpc_get_bits(3, 0, rate);
		amod->wy_nss = slsi_lpc_get_bits(6, 4, rate) + 1;
		amod->wy_gi = slsi_lpc_get_bits(18, 17, rate);
		amod->wy_ax_bw_idx_or_tone = slsi_lpc_get_bits(31, 24, rate);
		break;
	default:
		SLSI_WARN_NODEV("Unknown Mode Type\n");
		break;
	}

	amod->phy_rate = slsi_lpc_wyrate_get_phy_rate(amod);
}

#define MAC_TIMESTAMP_MAX (BIT(32))
#define CASE1_CASE2_DISTANCE (MAC_TIMESTAMP_MAX * 9 / 10)

static u64 slsi_lpc_mac_timestamp_latest = MAC_TIMESTAMP_MAX;
static u64 slsi_lpc_mac_timestamp_base;

static u64 slsi_lpc_get_mac_timestamp(u32 mactime)
{
	bool is_case1_or_case2;

	if (slsi_lpc_mac_timestamp_latest == MAC_TIMESTAMP_MAX) {
		slsi_lpc_mac_timestamp_latest = mactime;
		return mactime;
	}
	/* Assuming that "ts" doesn't go back more than  MAC_TIMESTAMP_MAX*1/10
	 * with reference to self._tstamp_latest, there are two corner cases.
	 * +-------------- MAC_TIMESTAMP_MAX ---------------------+
	 * |---R1--|------MAC_TIMESTAMP_MAX*9/10-----------|--R3--|
	 *    |ts                                             |_tstamp_latest : Case 1 (ts is in the future)
	 *    |_tstamp_latest                                 |ts             : Case 2 (ts comes from the past)
	 */
	is_case1_or_case2 = abs(mactime - slsi_lpc_mac_timestamp_latest) > CASE1_CASE2_DISTANCE;
	if (is_case1_or_case2) {
		if ((u64)mactime < slsi_lpc_mac_timestamp_latest) {
			slsi_lpc_mac_timestamp_base += MAC_TIMESTAMP_MAX;
			slsi_lpc_mac_timestamp_latest = mactime;
			return slsi_lpc_mac_timestamp_base + mactime;
		} else {
			return slsi_lpc_mac_timestamp_base - MAC_TIMESTAMP_MAX + mactime;
		}
	} else {
		slsi_lpc_mac_timestamp_latest = max_t(u64, slsi_lpc_mac_timestamp_latest, mactime);
		return slsi_lpc_mac_timestamp_base + mactime;
	}

	return 0;
}

static struct sk_buff *slsi_lpc_add_radiotap(struct sk_buff *skb, u32 mactime, u32 rate, u32 agg_id, bool is_last_mpdu, short rssi, short snr, int frequnecy)
{
	struct sk_buff *rt_skb;
	struct acc_mod_opt_decode acc_rate;
	struct slsi_lpc_radiotap_hdr rt;
	struct slsi_lpc_radiotap_hdr *prt;
	struct slsi_lpc_radiotap_mcs rt_mcs;
	struct slsi_lpc_radiotap_ampdu rt_ampdu;
	struct slsi_lpc_radiotap_vht rt_vht;
	u32 is_present = 0;
	int radiotap_len = 0;
	int bw_idx_map[9] = {0, 0, 1, 0, 4, 0, 0, 0, 11};

	memset(&rt, 0, sizeof(struct slsi_lpc_radiotap_hdr));
	memset(&rt_mcs, 0, sizeof(struct slsi_lpc_radiotap_mcs));
	memset(&rt_ampdu, 0, sizeof(struct slsi_lpc_radiotap_ampdu));
	memset(&rt_vht, 0, sizeof(struct slsi_lpc_radiotap_vht));

	slsi_lpc_accmod_opt_decode(rate, &acc_rate);

	if (!acc_rate.is_valid)
		goto skip_rate;

	switch(acc_rate.wy_phy) {
		case WYRATE_PHY_TYPE_11A:
			rt.rate = (u8)(acc_rate.phy_rate * 5);
			rt.channel.channel_flags |= cpu_to_le16(IEEE80211_CHAN_5GHZ);
			rt.channel.channel_flags |= cpu_to_le16(IEEE80211_CHAN_OFDM);
			break;
		case WYRATE_PHY_TYPE_11B:
			rt.rate = (u8)(acc_rate.phy_rate * 5);
			rt.channel.channel_flags |= cpu_to_le16(IEEE80211_CHAN_2GHZ);
			rt.channel.channel_flags |= cpu_to_le16(IEEE80211_CHAN_CCK);
			rt.flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
			break;
		case WYRATE_PHY_TYPE_11N:
			rt.channel.channel_flags |= cpu_to_le16(IEEE80211_CHAN_OFDM);
			if (frequnecy < CH14_OUT_BOUND)
				rt.channel.channel_flags |= cpu_to_le16(IEEE80211_CHAN_2GHZ);
			else
				rt.channel.channel_flags |= cpu_to_le16(IEEE80211_CHAN_5GHZ);
			rt_mcs.known |= IEEE80211_RADIOTAP_MCS_HAVE_BW;
			rt_mcs.flags |= bw_idx_map[acc_rate.wy_bw / 20];
			rt_mcs.known |= IEEE80211_RADIOTAP_MCS_HAVE_MCS;
			rt_mcs.mcs = acc_rate.wy_mcs;
			rt_mcs.known |= IEEE80211_RADIOTAP_MCS_HAVE_GI;
			rt_mcs.flags |= acc_rate.wy_gi << 2;
			rt_mcs.known |= IEEE80211_RADIOTAP_MCS_HAVE_FMT;
			rt_mcs.flags |= acc_rate.gf << 3;
			rt_mcs.known |= IEEE80211_RADIOTAP_MCS_HAVE_FEC;
			rt_mcs.flags |= acc_rate.wy_ldpc << 4;
			rt_mcs.known |= IEEE80211_RADIOTAP_MCS_HAVE_STBC;
			rt_mcs.flags |= acc_rate.wy_stbc << 5;
			break;
		case WYRATE_PHY_TYPE_11AC:
			rt_vht.known |= cpu_to_le16(IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH);
			rt_vht.bandwidth = bw_idx_map[acc_rate.wy_bw / 20];
			rt_vht.known |= cpu_to_le16(IEEE80211_RADIOTAP_VHT_KNOWN_STBC);
			rt_vht.flags |= acc_rate.wy_stbc;
			rt_vht.known |= cpu_to_le16(IEEE80211_RADIOTAP_VHT_KNOWN_GI);
			rt_vht.flags |= acc_rate.wy_gi = 0 ? (acc_rate.wy_gi << 2) : 0;
			rt_vht.mcs_nss[0] = acc_rate.wy_nss | (acc_rate.wy_mcs << 4);
			rt_vht.coding |= acc_rate.wy_ldpc;
			break;
		case WYRATE_PHY_TYPE_11AX:
			rt_vht.known |= cpu_to_le16(IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH);
			rt_vht.bandwidth = bw_idx_map[acc_rate.wy_bw / 20];
			rt_vht.known |= cpu_to_le16(IEEE80211_RADIOTAP_VHT_KNOWN_STBC);
			rt_vht.flags |= acc_rate.wy_stbc;
			rt_vht.known |= cpu_to_le16(IEEE80211_RADIOTAP_VHT_KNOWN_GI);
			rt_vht.flags |= acc_rate.wy_gi = 0 ? 0 : (acc_rate.wy_gi << 2);
			rt_vht.mcs_nss[0] = acc_rate.wy_nss | (acc_rate.wy_mcs << 4);
			rt_vht.coding |= acc_rate.wy_ldpc;
			break;
	}

skip_rate:
	rt_skb = alloc_skb(LPC_RADIOTAP_MAX_LEN, GFP_ATOMIC);
	if (!rt_skb) {
		SLSI_WARN_NODEV("Failed to allocate radiotap skb\n");
		return skb;
	}

	skb_put_data(rt_skb, &rt, sizeof(struct slsi_lpc_radiotap_hdr));
	radiotap_len += sizeof(struct slsi_lpc_radiotap_hdr);

	if (rssi != 0) {
		skb_put_u8(rt_skb, rssi);
		is_present |= 1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL;
		radiotap_len++;
	}

	if (snr != 0) {
		skb_put_u8(rt_skb, snr);
		is_present |= 1 << IEEE80211_RADIOTAP_DBM_ANTNOISE;
		radiotap_len++;
	}

	if (acc_rate.is_valid && acc_rate.wy_phy == WYRATE_PHY_TYPE_11N) {
		int pad = 0;
		if (radiotap_len % 4) {
			pad = 4 - (radiotap_len % 4);
		}
		skb_put_zero(rt_skb, pad);
		skb_put_data(rt_skb, &rt_mcs, sizeof(struct slsi_lpc_radiotap_mcs));
		is_present |= 1 << IEEE80211_RADIOTAP_MCS;
		radiotap_len += sizeof(struct slsi_lpc_radiotap_mcs) + pad;
	}

	if (agg_id) {
		int pad = 0;
		if (radiotap_len % sizeof(struct slsi_lpc_radiotap_ampdu)) {
			pad = sizeof(struct slsi_lpc_radiotap_ampdu) - (radiotap_len % sizeof(struct slsi_lpc_radiotap_ampdu));
		}
		skb_put_zero(rt_skb, pad);
		rt_ampdu.ampdu_flags |= cpu_to_le16(IEEE80211_RADIOTAP_AMPDU_LAST_KNOWN);
		rt_ampdu.ampdu_reference = cpu_to_le32(ampdu_agg_idx);
		if (is_last_mpdu) {
			rt_ampdu.ampdu_flags |= cpu_to_le16(IEEE80211_RADIOTAP_AMPDU_IS_LAST);
		}
		skb_put_data(rt_skb, &rt_ampdu, sizeof(struct slsi_lpc_radiotap_ampdu));
		is_present |= 1 << IEEE80211_RADIOTAP_AMPDU_STATUS;
		radiotap_len += sizeof(struct slsi_lpc_radiotap_ampdu) + pad;
	}

	if (acc_rate.is_valid && (acc_rate.wy_phy == WYRATE_PHY_TYPE_11AC || acc_rate.wy_phy == WYRATE_PHY_TYPE_11AX)) {
		int pad = 0;
		if (radiotap_len % 4) {
			pad = 4 - (radiotap_len % 4);
		}
		skb_put_zero(rt_skb, pad);
		skb_put_data(rt_skb, &rt_vht, sizeof(struct slsi_lpc_radiotap_vht));
		is_present |= 1 << IEEE80211_RADIOTAP_VHT;
		radiotap_len += sizeof(struct slsi_lpc_radiotap_vht) + pad;
	}
	
	prt = (struct slsi_lpc_radiotap_hdr *)rt_skb->data;

	prt->rt_tsft = cpu_to_le64(slsi_lpc_get_mac_timestamp(mactime));
	is_present |= 1 << IEEE80211_RADIOTAP_TSFT;
	is_present |= 1 << IEEE80211_RADIOTAP_FLAGS;
	prt->channel.channel_frequency = cpu_to_le16(frequnecy);
	is_present |= 1 << IEEE80211_RADIOTAP_CHANNEL;
	prt->hdr.it_len = cpu_to_le16 (radiotap_len);
	prt->hdr.it_present = cpu_to_le32(is_present);

	memcpy(skb_push(skb, rt_skb->len), rt_skb->data, rt_skb->len);
	kfree_skb(rt_skb);

	return skb;
}

static int slsi_lpc_send_mpdu_tx(struct lpc_type_mpdu_tx *mpdu_tx)
{
	struct sk_buff *c_skb = NULL;

	if (mpdu_tx->lpc_tag > 0 && mpdu_tx->lpc_tag < 0xffff) {
		struct lpc_skb_data *lpc_data = NULL;

		spin_lock_bh(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);
		lpc_data = slsi_lpc_find_packet_data(mpdu_tx->lpc_tag, &slsi_lpc_manager.lpc_tx_buffer_table[mpdu_tx->lpc_tag & (LPC_TABLE_SIZE - 1)]);
		spin_unlock_bh(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);
		if (lpc_data) {
			struct sk_buff *skb = NULL;
			spin_lock_bh(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);
			c_skb = slsi_lpc_delete_packet_data(mpdu_tx->lpc_tag, &slsi_lpc_manager.lpc_tx_buffer_table[mpdu_tx->lpc_tag & (LPC_TABLE_SIZE - 1)]);
			spin_unlock_bh(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);
			if (!c_skb) {
				return -1;
			}

			if (!slsi_lpc_manager.sta_dev) {
				kfree_skb(c_skb);
				c_skb = NULL;
				return -1;
			}

			if (!slsi_lpc_filter_capture_packet_type(((struct ieee80211_hdr *)c_skb->data)->frame_control)) {
				kfree_skb(c_skb);
				c_skb = NULL;
				return -1;
			}

			skb = alloc_skb(LPC_RADIOTAP_MAX_LEN + c_skb->len + LPC_SKB_PADDING, GFP_ATOMIC);
			if (!skb) {
				SLSI_ERR_NODEV("Alloc skb Failed\n");
				kfree_skb(c_skb);
				c_skb = NULL;
				return -1;
			}
			skb_reserve(skb, LPC_RADIOTAP_MAX_LEN);
			memcpy(skb_put(skb, c_skb->len), c_skb->data, c_skb->len);
			kfree_skb(c_skb);
			if (skb) {
				slsi_lpc_add_radiotap(skb, mpdu_tx->tx_mactime, mpdu_tx->tx_cfm_rate, 0, false, 0, 0, mpdu_tx->primary_freq / 2);
				spin_lock_bh(&slsi_lpc_manager.lpc_queue_lock);
				__skb_queue_tail(&slsi_lpc_manager.queue, skb);
				spin_unlock_bh(&slsi_lpc_manager.lpc_queue_lock);
				schedule_work(&slsi_lpc_manager.lpc_work);
			}
		}
	} else {
		if (mpdu_tx->mpdu_len != mpdu_tx->content_len && mpdu_tx->mpdu_len < LPC_MPDU_LENGHT_MAX) {
			SLSI_WARN_NODEV("Invalid mpdu_tx Wrong length. %x %x fc:%x mac_time:%x\n", 
				mpdu_tx->content_len, mpdu_tx->mpdu_len, ((struct ieee80211_hdr *)mpdu_tx->content)->frame_control, mpdu_tx->tx_mactime);
			return -1;
		}
		c_skb = alloc_skb(LPC_RADIOTAP_MAX_LEN + mpdu_tx->content_len + LPC_SKB_PADDING, GFP_ATOMIC);
		if (!c_skb) {
			SLSI_ERR_NODEV("Alloc skb Failed\n");
			return -1;
		}
		skb_reserve(c_skb, LPC_RADIOTAP_MAX_LEN);
		memcpy(skb_put(c_skb, mpdu_tx->content_len), mpdu_tx->content, mpdu_tx->content_len);
		if (!slsi_lpc_filter_capture_packet_type(((struct ieee80211_hdr *)c_skb->data)->frame_control)) {
			kfree_skb(c_skb);
			c_skb = NULL;
			return -1;
		}

		if ((ieee80211_is_nullfunc(((struct ieee80211_hdr *)c_skb->data)->frame_control))
			&& (mpdu_tx->flag16 & cpu_to_le16(LPC_TX_FLAG_PS_BIT_SET))) {
			((struct ieee80211_hdr *)c_skb->data)->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);
		}

		if (c_skb) {
			slsi_lpc_add_radiotap(c_skb, mpdu_tx->tx_mactime, mpdu_tx->tx_cfm_rate, 0, false, 0, 0, mpdu_tx->primary_freq / 2);
			spin_lock_bh(&slsi_lpc_manager.lpc_queue_lock);
			__skb_queue_tail(&slsi_lpc_manager.queue, c_skb);
			spin_unlock_bh(&slsi_lpc_manager.lpc_queue_lock);
			schedule_work(&slsi_lpc_manager.lpc_work);
		}
	}

	return 0;
}

static int slsi_lpc_make_tx_msdu_list(u32 start_tag, u8 aggs, struct sk_buff_head *msdu_list)
{
	struct lpc_skb_data *lpc_data = NULL;
	struct sk_buff *c_skb = NULL;
	int i, cnt = 0;
	u32 lpc_tag;

	if (start_tag < 1 || start_tag > 0xfffe)
		return 0;

	for (i = 0; i < aggs; i++) {
		lpc_tag = start_tag + i;
		spin_lock_bh(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);
		lpc_data = slsi_lpc_find_packet_data(lpc_tag, &slsi_lpc_manager.lpc_tx_buffer_table[lpc_tag & (LPC_TABLE_SIZE - 1)]);
		if (lpc_data) {
			c_skb = slsi_lpc_delete_packet_data(lpc_tag, &slsi_lpc_manager.lpc_tx_buffer_table[lpc_tag & (LPC_TABLE_SIZE - 1)]);
			if (c_skb) {
				__skb_queue_tail(msdu_list, c_skb);
				cnt++;
			}
		}
		spin_unlock_bh(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);
	}
	return cnt;
}

static struct sk_buff *slsi_lpc_data_header_amsdu_tx(struct lpc_type_ampdu_tx *ampdu_tx, int idx, struct sk_buff *skb)
{
	struct ieee80211_hdr_3addr tx_header;
	u16 mpdu_fc = (ampdu_tx->fc & 65295) | (ampdu_tx->subtype[idx] << 4); /* 65295 = 0xFF0F */

	memset(&tx_header, 0, sizeof(struct ieee80211_hdr_3addr));
	tx_header.frame_control = cpu_to_le16(mpdu_fc);
	SLSI_ETHER_COPY(tx_header.addr1, (u8 *)ampdu_tx->ra);
	SLSI_ETHER_COPY(tx_header.addr2, (u8 *)ampdu_tx->ta);
	SLSI_ETHER_COPY(tx_header.addr3, (u8 *)ampdu_tx->ra);
	tx_header.seq_ctrl = cpu_to_le16((ampdu_tx->start_seq + idx) << 4);
	if (ieee80211_has_order(ampdu_tx->fc))
		memcpy(skb_push(skb, IEEE80211_HT_CTL_LEN), &ampdu_tx->rts_rate, IEEE80211_HT_CTL_LEN);
	if (ieee80211_is_data_qos(ampdu_tx->fc)) {
		u16 qos_ctl = 0;
		qos_ctl |= cpu_to_le16(ampdu_tx->tid) | cpu_to_le16(IEEE80211_QOS_CTL_A_MSDU_PRESENT);
		memcpy(skb_push(skb, IEEE80211_QOS_CTL_LEN), &qos_ctl, IEEE80211_QOS_CTL_LEN);
	}

	memcpy(skb_push(skb, sizeof(struct ieee80211_hdr_3addr)), &tx_header, sizeof(struct ieee80211_hdr_3addr));

	return skb;
}

static struct sk_buff *slsi_lpc_mgmt_header_amsdu_tx(struct lpc_type_ampdu_tx *ampdu_tx, int idx, struct sk_buff *skb)
{
	struct ieee80211_hdr_3addr tx_header;
	u16 mpdu_fc = (ampdu_tx->fc & 65295) | (ampdu_tx->subtype[idx] << 4); /* 65295 = 0xFF0F */

	memset(&tx_header, 0, sizeof(struct ieee80211_hdr_3addr));
	tx_header.frame_control = cpu_to_le16(mpdu_fc);
	SLSI_ETHER_COPY(tx_header.addr1, (u8 *)ampdu_tx->ra);
	SLSI_ETHER_COPY(tx_header.addr2, (u8 *)ampdu_tx->ta);
	SLSI_ETHER_COPY(tx_header.addr3, (u8 *)ampdu_tx->ra);
	tx_header.seq_ctrl = cpu_to_le16((ampdu_tx->start_seq + idx) << 4);
	if (ieee80211_has_order(ampdu_tx->fc))
		memcpy(skb_push(skb, IEEE80211_HT_CTL_LEN), &ampdu_tx->rts_rate, IEEE80211_HT_CTL_LEN);
	memcpy(skb_push(skb, sizeof(struct ieee80211_hdr_3addr)), &tx_header, sizeof(struct ieee80211_hdr_3addr));

	return skb;
}

static int slsi_lpc_find_last_mpdu_in_ampdu_tx(u64 req_bitmap[LPC_NUM_BITMAP_ARRAY_NUM_AMPDU_TX])
{
	int i;
	for (i = LPC_NUM_MPDU_MAX_IN_AMPDU_TX - 1; i >= 0; i--) {
		int idx = i / 64;
		int bit = i % 64;
		if (req_bitmap[idx] & BIT_ULL(bit))
			return i;
	}
	return -EINVAL;
}

static int slsi_lpc_send_ampdu_tx(struct lpc_type_ampdu_tx *ampdu_tx)
{
	struct sk_buff *c_skb = NULL;
	struct sk_buff *skb = NULL;
	struct sk_buff_head msdu_list, mpdu_list;
	int copy_skb_hdr_size = LPC_RADIOTAP_MAX_LEN + sizeof(struct ieee80211_qos_hdr) + 50;
	u32 payload_len = 0;
	int i, is_last_mpdu_idx;
	bool is_length_exceeded = false;

	if (!slsi_lpc_filter_capture_packet_type(ampdu_tx->fc))
		return -1;

	__skb_queue_head_init(&mpdu_list);
	ampdu_agg_idx = (ampdu_agg_idx + 1) % (BIT(32) - 1);
	if (ampdu_agg_idx == 0)
		ampdu_agg_idx = 1;

	is_last_mpdu_idx = slsi_lpc_find_last_mpdu_in_ampdu_tx(ampdu_tx->req_bitmap);
	if (is_last_mpdu_idx < 0) {
		if (lpc_debug_mode)
			SLSI_WARN_NODEV("AMPDU TX req_bitmap is all 0.\n");
		return -1;
	}

	for (i = 0; i < LPC_NUM_MPDU_MAX_IN_AMPDU_TX; i++) {
		int idx = i / 64;
		int bit = i % 64;

		if (!(ampdu_tx->req_bitmap[idx] & BIT_ULL(bit)))
			continue;

		is_length_exceeded = false;

		ampdu_tx->fc &= ~IEEE80211_FCTL_RETRY;
		if (ampdu_tx->retry_bitmap[idx] & BIT_ULL(bit)) {
			ampdu_tx->fc |= IEEE80211_FCTL_RETRY;
		}
		__skb_queue_head_init(&msdu_list);

		if (ampdu_tx->mpdu_len[i] == 0) {
			if (lpc_debug_mode)
				SLSI_WARN_NODEV("mpdu_len[%d] is %d.\n", i, ampdu_tx->mpdu_len[i]);
			continue;
		}

		if (!slsi_lpc_make_tx_msdu_list(ampdu_tx->tx_tag_list[i], ampdu_tx->fw_agg_tags[i], &msdu_list))
			continue;

		payload_len = 0;
		c_skb = alloc_skb(copy_skb_hdr_size + ampdu_tx->mpdu_len[i] + LPC_SKB_PADDING, GFP_ATOMIC);
		if (!c_skb) {
			SLSI_ERR_NODEV("Alloc ampdu tx skb Failed\n");
			while (!skb_queue_empty(&msdu_list)) {
				skb = __skb_dequeue(&msdu_list);
				kfree_skb(skb);
			}
			continue;
		}
		skb_reserve(c_skb, copy_skb_hdr_size);
		while (!skb_queue_empty(&msdu_list)) {
			skb = __skb_dequeue(&msdu_list);
			if (ampdu_tx->mpdu_len[i] > payload_len + skb->len) {
				skb_put_data(c_skb, skb->data, skb->len);
				if (skb->len % 4 != 0) {
					int pad = 4 - (skb->len % 4);
					memset(skb_put(c_skb, pad), 0, pad);
				}
				payload_len += skb->len;
			} else if (!is_length_exceeded) {
				SLSI_WARN_NODEV("mpdu_len[%d]:%d payload_len:%d skb->len:%d\n", i, ampdu_tx->mpdu_len[i], payload_len, skb->len);
				is_length_exceeded = true;
			}
			kfree_skb(skb);
		}

		if (ieee80211_is_data(ampdu_tx->fc))
			c_skb = slsi_lpc_data_header_amsdu_tx(ampdu_tx, i, c_skb);
		else if (ieee80211_is_mgmt(ampdu_tx->fc))
			c_skb = slsi_lpc_mgmt_header_amsdu_tx(ampdu_tx, i, c_skb);
		else {
			kfree_skb(c_skb);
			c_skb = NULL;
		}

		if (!c_skb)
			continue;

		if (i == is_last_mpdu_idx) {
			/* LAST AMPDU */
			slsi_lpc_add_radiotap(c_skb, ampdu_tx->tx_mactime, ampdu_tx->tx_rate, ampdu_agg_idx, true, 0, 0, ampdu_tx->primary_freq / 2);
		} else {
			slsi_lpc_add_radiotap(c_skb, ampdu_tx->tx_mactime, ampdu_tx->tx_rate, ampdu_agg_idx, false, 0, 0, ampdu_tx->primary_freq / 2);
		}

		spin_lock_bh(&slsi_lpc_manager.lpc_queue_lock);
		__skb_queue_tail(&slsi_lpc_manager.queue, c_skb);
		spin_unlock_bh(&slsi_lpc_manager.lpc_queue_lock);
		schedule_work(&slsi_lpc_manager.lpc_work);
	}

	return 0;
}

static struct sk_buff *slsi_lpc_send_mpdu_rx(struct lpc_type_mpdu_rx *mpdu_rx)
{
	struct sk_buff *c_skb = NULL;

	if (mpdu_rx->lpc_tag > 0 && mpdu_rx->lpc_tag < 0xffff) {
		struct lpc_skb_data *lpc_data = NULL;

		spin_lock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
		lpc_data = slsi_lpc_find_packet_data(mpdu_rx->lpc_tag, &slsi_lpc_manager.lpc_rx_buffer_table[mpdu_rx->lpc_tag & (LPC_TABLE_SIZE - 1)]);
		spin_unlock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
		if (lpc_data) {
			struct sk_buff *skb = NULL;
			spin_lock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
			c_skb = slsi_lpc_delete_packet_data(mpdu_rx->lpc_tag, &slsi_lpc_manager.lpc_rx_buffer_table[mpdu_rx->lpc_tag & (LPC_TABLE_SIZE - 1)]);
			spin_unlock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
			if (!c_skb) {
				goto exit;
			}

			if (!slsi_lpc_manager.sta_dev) {
				kfree_skb(c_skb);
				c_skb = NULL;
				goto exit;
			}

			if (!slsi_lpc_filter_capture_packet_type(((struct ieee80211_hdr *)c_skb->data)->frame_control)) {
				kfree_skb(c_skb);
				c_skb = NULL;
				goto exit;
			}

			skb = alloc_skb(LPC_RADIOTAP_MAX_LEN + c_skb->len + LPC_SKB_PADDING, GFP_ATOMIC);
			if (!skb) {
				SLSI_ERR_NODEV("Alloc skb Failed\n");
				kfree_skb(c_skb);
				c_skb = NULL;
				goto exit;
			}
			skb_reserve(skb, LPC_RADIOTAP_MAX_LEN);
			memcpy(skb_put(skb, c_skb->len), c_skb->data, c_skb->len);
			kfree_skb(c_skb);
			c_skb = skb;
		}
	} else {
		if (mpdu_rx->mpdu_len != mpdu_rx->content_len && mpdu_rx->mpdu_len < LPC_MPDU_LENGHT_MAX) {
			SLSI_WARN_NODEV("Invalid mpdu_rx Wrong length %x %x\n", mpdu_rx->content_len, mpdu_rx->mpdu_len);
			goto exit;
		}

		c_skb = alloc_skb(LPC_RADIOTAP_MAX_LEN + mpdu_rx->content_len + LPC_SKB_PADDING, GFP_ATOMIC);
		if (!c_skb) {
			SLSI_ERR_NODEV("Alloc skb Failed\n");
			goto exit;
		}
		skb_reserve(c_skb, LPC_RADIOTAP_MAX_LEN);
		memcpy(skb_put(c_skb, mpdu_rx->content_len), mpdu_rx->content, mpdu_rx->content_len);
		if (!slsi_lpc_manager.sta_dev) {
			kfree_skb(c_skb);
			c_skb = NULL;
			goto exit;
		}

		if (!slsi_lpc_filter_capture_packet_type(((struct ieee80211_hdr *)c_skb->data)->frame_control)) {
			kfree_skb(c_skb);
			c_skb = NULL;
		}
	}
exit:
	return c_skb;
}

static int slsi_lpc_make_rx_amsdu_list(struct lpc_type_ampdu_rx *ampdu_rx, struct sk_buff_head *amsdu_list, struct sk_buff_head *info_list)
{
	struct sk_buff *skb, *c_skb = NULL;
	int i, cnt = 0;
	int num_mpdu = ampdu_rx->num_mpdu;

	if (ampdu_rx->lpc_tag > 0 && ampdu_rx->lpc_tag < 0xffff) {
		struct lpc_skb_data *lpc_data = NULL;
		u32 lpc_tag;

		for (i = 0; i < num_mpdu; i++) {
			lpc_tag = ampdu_rx->lpc_tag + i;
			if (lpc_tag == LPC_TAG_NUM_MAX)
				lpc_tag = 1;
			skb = NULL;
			spin_lock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
			lpc_data = slsi_lpc_find_packet_data(lpc_tag, &slsi_lpc_manager.lpc_rx_buffer_table[lpc_tag & (LPC_TABLE_SIZE - 1)]);
			spin_unlock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
			if (lpc_data) {
				spin_lock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
				skb = slsi_lpc_delete_packet_data(lpc_tag, &slsi_lpc_manager.lpc_rx_buffer_table[lpc_tag & (LPC_TABLE_SIZE - 1)]);
				spin_unlock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
				if (!skb)
					continue;

				if (!slsi_lpc_filter_capture_packet_type(((struct ieee80211_hdr *)skb->data)->frame_control)) {
					kfree_skb(skb);
					skb = NULL;
				}
				if (skb) {
					c_skb = alloc_skb(LPC_RADIOTAP_MAX_LEN + skb_end_offset(skb) + skb->data_len, GFP_ATOMIC);
					if (!c_skb) {
						SLSI_ERR_NODEV("Alloc skb Failed\n");
						kfree_skb(skb);
						continue;
					}
					skb_reserve(c_skb, LPC_RADIOTAP_MAX_LEN);
					memcpy(skb_put(c_skb, skb->len), skb->data, skb->len);
					kfree_skb(skb);
					__skb_queue_tail(amsdu_list, c_skb);
					cnt++;
				}
			} else {
				int size = sizeof(struct lpc_type_ampdu_rx);

				skb = alloc_skb(LPC_RADIOTAP_MAX_LEN + size + LPC_SKB_PADDING, GFP_ATOMIC);
				if (!skb) {
					SLSI_ERR_NODEV("Alloc skb Failed\n");
					continue;
				}
				ampdu_rx->lpc_tag = lpc_tag;
				skb_reserve(skb, LPC_RADIOTAP_MAX_LEN);
				memcpy(skb_put(skb, size), ampdu_rx, size);
				__skb_queue_tail(info_list, skb);
				cnt++;
			}
		}
	}
	return cnt;
}

static void slsi_lpc_send_ampdu_rx(struct lpc_type_phy_dollop *pd, struct sk_buff_head *amsdu_list)
{
	struct sk_buff *skb = NULL;
	int cnt = 0;

	ampdu_agg_idx = (ampdu_agg_idx + 1) % (BIT(32) - 1);
	if (ampdu_agg_idx == 0)
		ampdu_agg_idx = 1;

	while (!skb_queue_empty(amsdu_list)) {
		bool is_last_mpdu = false;
		skb = __skb_dequeue(amsdu_list);

		if (skb_queue_empty(amsdu_list))
			is_last_mpdu = true;
		if (pd) {
			slsi_lpc_add_radiotap(skb, pd->rx_start_mactime, pd->rx_rate, ampdu_agg_idx, is_last_mpdu, pd->rssi, pd->snr, pd->primary_freq / 2);
		} else {
			slsi_lpc_add_radiotap(skb, 0, 0, ampdu_agg_idx, is_last_mpdu, 0, 0, 0);
		}
		spin_lock_bh(&slsi_lpc_manager.lpc_queue_lock);
		__skb_queue_tail(&slsi_lpc_manager.queue, skb);
		spin_unlock_bh(&slsi_lpc_manager.lpc_queue_lock);
		cnt++;
	}
	if (cnt)
		schedule_work(&slsi_lpc_manager.lpc_work);
}

static void slsi_lpc_save_ampdu_rx_info(struct lpc_type_phy_dollop *pd, struct sk_buff_head *info_list)
{
	struct sk_buff *skb = NULL;

	ampdu_agg_idx = (ampdu_agg_idx + 1) % (BIT(32) - 1);
	if (ampdu_agg_idx == 0)
		ampdu_agg_idx = 1;

	while (!skb_queue_empty(info_list)) {
		struct lpc_type_ampdu_rx * ampdu_rx;
		u32 lpc_tag;
		bool is_last_mpdu = false;

		skb = __skb_dequeue(info_list);
		ampdu_rx = (struct lpc_type_ampdu_rx *)skb->data;
		lpc_tag = ampdu_rx->lpc_tag;

		kfree_skb(skb);
		skb = alloc_skb(LPC_RADIOTAP_MAX_LEN + LPC_SKB_PADDING, GFP_ATOMIC);
		if (!skb) {
			SLSI_ERR_NODEV("Alloc skb failed\n");
			continue;
		}
		skb_reserve(skb, LPC_RADIOTAP_MAX_LEN);

		if (skb_queue_empty(info_list))
			is_last_mpdu = true;
		if (pd) {
			slsi_lpc_add_radiotap(skb, pd->rx_start_mactime, pd->rx_rate, ampdu_agg_idx, is_last_mpdu, pd->rssi, pd->snr, pd->primary_freq / 2);
		} else {
			slsi_lpc_add_radiotap(skb, 0, 0, ampdu_agg_idx, is_last_mpdu, 0, 0, 0);
		}

		slsi_lpc_add_packet_data(lpc_tag, skb, SLSI_LPC_DATA_TYPE_AMPDU_RX_INFO);
		kfree_skb(skb);
	}
}

int slsi_lpc_send_ampdu_rx_later(u32 lpc_tag, struct sk_buff *skb)
{
	int ret = 0;
	struct sk_buff *f_skb = NULL;
	struct sk_buff *c_skb = NULL;
	struct sk_buff *skb2 = NULL;
	struct sk_buff *t_skb = skb;
	struct lpc_skb_data *lpc_data = NULL;
	struct slsi_skb_cb   *skb_cb = (struct slsi_skb_cb *)skb->cb;

	spin_lock_bh(&slsi_lpc_manager.lpc_ampdu_info_lock);
	lpc_data = slsi_lpc_find_packet_data(lpc_tag, &slsi_lpc_manager.lpc_rx_ampdu_info_table[lpc_tag & (LPC_TABLE_SIZE - 1)]);
	spin_unlock_bh(&slsi_lpc_manager.lpc_ampdu_info_lock);
	if (lpc_data) {
		spin_lock_bh(&slsi_lpc_manager.lpc_ampdu_info_lock);
		f_skb = slsi_lpc_delete_packet_data(lpc_tag, &slsi_lpc_manager.lpc_rx_ampdu_info_table[lpc_tag & (LPC_TABLE_SIZE - 1)]);
		spin_unlock_bh(&slsi_lpc_manager.lpc_ampdu_info_lock);
		if (!f_skb) {
			goto exit;
		}

		if (!slsi_lpc_filter_capture_packet_type(((struct ieee80211_hdr *)skb->data)->frame_control)) {
			kfree_skb(f_skb);
			f_skb = NULL;
			goto exit;
		}

		if (skb_has_frag_list(skb)) {
			struct sk_buff *frag_skb;
			unsigned int frag_len;
			skb2 = alloc_skb(skb->len + 4, GFP_ATOMIC);
			if (!skb2) {
				SLSI_ERR_NODEV("Error allocating skb2 %d bytes\n", skb->len);
				kfree_skb(f_skb);
				f_skb = NULL;
				goto exit;
			}
			frag_len = (unsigned char *)skb_tail_pointer(skb) - skb->data;
			if (frag_len)
				memcpy(skb_put(skb2, frag_len), skb->data, frag_len);

			skb_walk_frags(skb, frag_skb) {
				frag_len = (unsigned char *)skb_tail_pointer(frag_skb) - frag_skb->data;
				if (frag_len)
					memcpy(skb_put(skb2, frag_len), frag_skb->data, frag_len);
			}
			memcpy(skb2->cb, skb->cb, sizeof(struct slsi_skb_cb));
			t_skb = skb2;
		}

		if (skb_cb->is_ciphered) {
			int offset = slsi_lpc_get_key_rsc_offset_in_amsdu(skb);
			unsigned char *data_at_offset = t_skb->data + offset;

			if (offset < SLSI_EAPOL_KEY_RSC_LENGTH) {
				SLSI_WARN_NODEV("Wrong key_rsc offset.\n");
				kfree_skb(f_skb);
				f_skb = NULL;
				goto exit;
			}

			c_skb = alloc_skb(LPC_RADIOTAP_MAX_LEN + skb_end_offset(t_skb) + LPC_SKB_PADDING, GFP_ATOMIC);
			if (!c_skb) {
				SLSI_ERR_NODEV("Alloc skb Failed\n");
				kfree_skb(f_skb);
				f_skb = NULL;
				goto exit;
			}

			skb_reserve(c_skb, LPC_RADIOTAP_MAX_LEN);
			memcpy(skb_put(c_skb, offset - SLSI_EAPOL_KEY_RSC_LENGTH), t_skb->data, offset - SLSI_EAPOL_KEY_RSC_LENGTH);
			memcpy(skb_put(c_skb, t_skb->len - offset), data_at_offset, t_skb->len - offset);
			memset(skb_put(c_skb, 4), 0, 4);
			memcpy(skb_push(c_skb, f_skb->len), f_skb->data, f_skb->len);
			kfree_skb(f_skb);
			f_skb = NULL;
		} else {
			c_skb = alloc_skb(LPC_RADIOTAP_MAX_LEN + skb_end_offset(t_skb) + LPC_SKB_PADDING, GFP_ATOMIC);
			if (!c_skb) {
				SLSI_ERR_NODEV("Alloc skb Failed\n");
				kfree_skb(f_skb);
				f_skb = NULL;
				goto exit;
			}
			skb_reserve(c_skb, LPC_RADIOTAP_MAX_LEN);
			memcpy(skb_put(c_skb, t_skb->len), t_skb->data, t_skb->len);
			memcpy(skb_push(c_skb, f_skb->len), f_skb->data, f_skb->len);
			kfree_skb(f_skb);
			f_skb = NULL;
		}
		spin_lock_bh(&slsi_lpc_manager.lpc_queue_lock);
		__skb_queue_tail(&slsi_lpc_manager.queue, c_skb);
		spin_unlock_bh(&slsi_lpc_manager.lpc_queue_lock);
		schedule_work(&slsi_lpc_manager.lpc_work);
		ret = 1;
	}
exit:
	if (skb2)
		kfree_skb(skb2);
	return ret;
}

static bool slsi_lpc_is_required_read_index_change(u32 r, u32 buffer_size, u16 next_type)
{
	if (r + sizeof(struct tlv_hdr) > buffer_size)
		return true;
	if (next_type == LPC_TYPE_PADDING)
		return true;
	return false;
}

static bool slsi_lpc_check_last_packet(void *log_buffer, u32 r, u32 w, u32 buffer_size)
{
	struct tlv_hdr *tlv_data;
	int i;

	for (i = 0; i < 2; i++) {
		tlv_data = (struct tlv_hdr *)(log_buffer + r);
		r += sizeof(struct tlv_hdr) + tlv_data->len;
		if (slsi_lpc_is_required_read_index_change(r, buffer_size, tlv_data->type))
			r = 0;
		if (r == w)
			return true;
	}
	return false;
}

int slsi_lpc_get_packets_info(struct slsi_dev *sdev)
{
	void *log_buffer = NULL;
	struct log_descriptor *lpc_ld;
	struct tlv_container *tlv_con, *tlv_con_fw;
	struct tlv_hdr *tlv_data, *next_tlv;
	void *log_start, *log_end;
	int todo_cnt = LPC_ITERATION_MAX;
	u32 cnt = 0, distance = 0;
	u32 r, w, bufsize;
	bool outflag = false;

	if (!slsi_lpc_manager.enable)
		return todo_cnt;

	log_buffer = scsc_service_mxlogger_buff(sdev->service);

	if (!log_buffer) {
		SLSI_ERR_NODEV("Can't get TLV Container\n");
		return todo_cnt;
	}

	lpc_ld = (struct log_descriptor *)log_buffer;
	if (lpc_ld->magic_number != LPC_STORAGE_DESCRIPTOR_MAGIC_NUMBER ||
	    lpc_ld->version_major != LPC_STORAGE_DESCRIPTOR_VERSION_MAJOR ||
	    lpc_ld->version_minor != LPC_STORAGE_DESCRIPTOR_VERSION_MINOR) {
		SLSI_ERR_NODEV("Wrong log_descriptor\n");
		return todo_cnt;
	}

	log_buffer += sizeof(struct log_descriptor);

	tlv_con = (struct tlv_container *)log_buffer;
	tlv_con->magic_number = LPC_TLV_CONTAINER_MAGIC_NUMBER;
	r = tlv_con->r_index;

	log_buffer += sizeof(struct tlv_container);
	log_buffer += tlv_con->buffer_size;

	tlv_con_fw = (struct tlv_container *)log_buffer;
	if (tlv_con_fw->magic_number != LPC_TLV_CONTAINER_MAGIC_NUMBER_FW) {
		SLSI_ERR_NODEV("Wrong tlv container\n");
		return todo_cnt;
	}
	log_buffer += sizeof(struct tlv_container);

	log_start = log_buffer;
	log_end = log_start + tlv_con_fw->buffer_size;
	bufsize = tlv_con_fw->buffer_size;
	w = tlv_con_fw->w_index;

	while (r != w && todo_cnt > 0) {
		struct sk_buff_head amsdu_list, info_list;
		struct sk_buff *c_skb = NULL;
		int amsdu_list_cnt;
		next_tlv = NULL;
		distance = 0;

		tlv_data = (struct tlv_hdr *)(log_buffer + r);

		if (slsi_lpc_is_required_read_index_change(r, bufsize, tlv_data->type)) {
			r = 0;
			tlv_data = (struct tlv_hdr *)(log_buffer + r);
		}

		if (w >= r)
			distance = w - r;
		else 
			distance = bufsize - r + w;

		if (distance < lpc_distance_guard) {
			next_tlv = (struct tlv_hdr *)(log_buffer + r);
			break;
		}

		if (slsi_lpc_check_last_packet(log_buffer, r, w, bufsize)) {
			next_tlv = (struct tlv_hdr *)(log_buffer + r);
			break;
		}

		switch (tlv_data->type) {
		case LPC_TYPE_MPDU_TX:
			tlv_data->type = LPC_TYPE_PAST_TLV;
			r += sizeof(struct tlv_hdr);
			slsi_lpc_send_mpdu_tx((struct lpc_type_mpdu_tx *)(log_buffer + r));
			r += tlv_data->len;
			break;
		case LPC_TYPE_AMPDU_TX:
			tlv_data->type = LPC_TYPE_PAST_TLV;
			if (tlv_data->len + sizeof(struct tlv_hdr) != sizeof(struct lpc_type_ampdu_tx)) {
				SLSI_WARN_NODEV("AMPDU_TX TLV length is mismatch:0x%x 0x%x\n", tlv_data->len, sizeof(struct lpc_type_ampdu_tx));
				r += sizeof(struct tlv_hdr) + tlv_data->len;
				break;
			}
			r += sizeof(struct tlv_hdr);
			slsi_lpc_send_ampdu_tx((struct lpc_type_ampdu_tx *)(log_buffer + r));
			r += tlv_data->len;
			break;
		case LPC_TYPE_MPDU_RX:
			tlv_data->type = LPC_TYPE_PAST_TLV;
			r += sizeof(struct tlv_hdr);
			c_skb = slsi_lpc_send_mpdu_rx((struct lpc_type_mpdu_rx *)(log_buffer + r));
			r += tlv_data->len;
			if (c_skb) {
				struct lpc_type_mpdu_rx *mpdu_rx = (struct lpc_type_mpdu_rx *)(log_buffer + r - tlv_data->len);
				struct lpc_type_phy_dollop *pd;
				tlv_data = (struct tlv_hdr *)(log_buffer + r);
				if (tlv_data->type == LPC_TYPE_PHY_DOLLOP) {
					tlv_data->type = LPC_TYPE_PAST_TLV;
					r += sizeof(struct tlv_hdr);
					pd = (struct lpc_type_phy_dollop *)(log_buffer + r);
					slsi_lpc_add_radiotap(c_skb, pd->rx_start_mactime, pd->rx_rate, 0, false, pd->rssi, pd->snr, pd->primary_freq / 2);
					r += tlv_data->len;
					cnt++;
					todo_cnt--;
				} else {
					slsi_lpc_add_radiotap(c_skb, mpdu_rx->systime, 0, 0, false, 0, 0, 0);
				}

				spin_lock_bh(&slsi_lpc_manager.lpc_queue_lock);
				__skb_queue_tail(&slsi_lpc_manager.queue, c_skb);
				spin_unlock_bh(&slsi_lpc_manager.lpc_queue_lock);
				schedule_work(&slsi_lpc_manager.lpc_work);
			}
			break;
		case LPC_TYPE_AMPDU_RX:
			tlv_data->type = LPC_TYPE_PAST_TLV;
			if (tlv_data->len + sizeof(struct tlv_hdr) != sizeof(struct lpc_type_ampdu_rx)) {
				SLSI_WARN_NODEV("AMPDU_RX TLV length is mismatch:0x%x 0x%x\n", tlv_data->len, sizeof(struct lpc_type_ampdu_rx));
				r += sizeof(struct tlv_hdr) + tlv_data->len;
				break;
			}
			__skb_queue_head_init(&amsdu_list);
			__skb_queue_head_init(&info_list);
			r += sizeof(struct tlv_hdr);
			amsdu_list_cnt = slsi_lpc_make_rx_amsdu_list((struct lpc_type_ampdu_rx *)(log_buffer + r), &amsdu_list, &info_list);
			r += tlv_data->len;
			if (amsdu_list_cnt) {
				tlv_data = (struct tlv_hdr *)(log_buffer + r);
				if (tlv_data->type == LPC_TYPE_PHY_DOLLOP) {
					tlv_data->type = LPC_TYPE_PAST_TLV;
					r += sizeof(struct tlv_hdr);
					slsi_lpc_send_ampdu_rx((struct lpc_type_phy_dollop *)(log_buffer + r), &amsdu_list);
					slsi_lpc_save_ampdu_rx_info((struct lpc_type_phy_dollop *)(log_buffer + r), &info_list);
					r += tlv_data->len;
					cnt++;
					todo_cnt--;
				} else {
					slsi_lpc_send_ampdu_rx(NULL, &amsdu_list);
					slsi_lpc_save_ampdu_rx_info(NULL, &info_list);
				}
			}
			break;
		case LPC_TYPE_PHY_DOLLOP:
			tlv_data->type = LPC_TYPE_PAST_TLV;
			r += sizeof(struct tlv_hdr) + tlv_data->len;
			break;
		default:
			SLSI_WARN_NODEV("Type(%x) is incorrect and waiting for the correct type. len:%x\n", tlv_data->type, tlv_data->len);
			outflag = true;
			break;
		}

		if (outflag)
			break;

		cnt++;
		todo_cnt--;
	}

	tlv_con->of_tlvs += cnt;
	tlv_con->w_index = w;

	
	if (next_tlv)
		SLSI_INFO_NODEV("done:%d, start_ridx:%x, next_ridx%x, widx:%x, cnt:%x, distance(w-r):%d [next type:%x len:%x]\n",
			cnt, tlv_con->r_index, r, w, tlv_con->of_tlvs, distance, next_tlv->type, next_tlv->len);
	else
		SLSI_INFO_NODEV("done:%d, start_ridx:%x, next_ridx%x, widx:%x, cnt:%x, distance(w-r):%d\n",
			cnt, tlv_con->r_index, r, w, tlv_con->of_tlvs, distance);

	tlv_con->r_index = r;
	/* CPU memory barrier */
	wmb();
	return todo_cnt;
}

static int slsi_lpc_set_mib_local_packet_capture_mode(struct slsi_dev *sdev, bool value)
{
	struct slsi_mib_data mib_data = {0, NULL};
	int ret;

	ret = slsi_mib_encode_bool(&mib_data, SLSI_PSID_UNIFI_LOCAL_PACKET_CAPTURE_MODE, value, 0);
	if (ret != SLSI_MIB_STATUS_SUCCESS) {
		SLSI_ERR(sdev, "LPC Failed: no mem for MIB\n");
		return -ENOMEM;
	}
	ret = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);
	if (ret)
		SLSI_ERR(sdev, "Enable/Disable LPC mode failed. %d\n", ret);

	kfree(mib_data.data);
	return ret;
}

static int slsi_lpc_net_open(struct net_device *dev)
{
	return 0;
}

static int slsi_lpc_net_stop(struct net_device *dev)
{
	return 0;
}

static netdev_tx_t slsi_lpc_net_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	kfree_skb(skb);
	return 0;
}

static const struct net_device_ops slsi_lpc_netdev_ops = {
	.ndo_open         = slsi_lpc_net_open,
	.ndo_stop         = slsi_lpc_net_stop,
	.ndo_start_xmit   = slsi_lpc_net_start_xmit,
};

static void slsi_lpc_dev_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &slsi_lpc_netdev_ops;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 9))
	dev->needs_free_netdev = true;
#else
	dev->destructor = free_netdev;
#endif
#ifndef CONFIG_WLBT_WARN_ON
	dev->watchdog_timeo = SLSI_TX_TIMEOUT;
#endif
}

int slsi_lpc_start(struct slsi_dev *sdev, int type, const char *lpc_name)
{
	int ret = LPC_UNKNOWN_ERROR;

	if (slsi_lpc_manager.enable) {
		SLSI_INFO(sdev, "Local Packet Capture %s is already Running.\n", slsi_lpc_manager.lpc_dev->name);
		return LPC_START_CAPTURE_IS_ALREADY_RUNNING; /* Capture is already running */
	}

	if(slsi_lpc_manager.lpc_dev) {
		SLSI_WARN_NODEV("Remove the lpc_dev(%s) that has not been deleted before.\n", slsi_lpc_manager.lpc_dev->name);
		unregister_netdevice(slsi_lpc_manager.lpc_dev);
		free_netdev(slsi_lpc_manager.lpc_dev);
		slsi_lpc_manager.lpc_dev = NULL;
	}

	slsi_lpc_manager.lpc_dev = alloc_netdev(0, lpc_name, NET_NAME_PREDICTABLE, slsi_lpc_dev_setup);
	if (!slsi_lpc_manager.lpc_dev) {
		SLSI_ERR(sdev, "Failed to allocate private data for netdev\n");
		return LPC_START_NOT_ENOUGH_MEMORY;
	}

	ret = dev_alloc_name(slsi_lpc_manager.lpc_dev, slsi_lpc_manager.lpc_dev->name);
	if (ret < 0) {
		SLSI_ERR(sdev, "Failed to allocate private data for netdev\n");
		free_netdev(slsi_lpc_manager.lpc_dev);
		slsi_lpc_manager.lpc_dev = NULL;
		return LPC_UNKNOWN_ERROR;
	}

	ret = register_netdevice(slsi_lpc_manager.lpc_dev);
	if (ret) {
		SLSI_ERR(sdev, "Register lpc_dev(%s) failed\n");
		free_netdev(slsi_lpc_manager.lpc_dev);
		slsi_lpc_manager.lpc_dev = NULL;
		return LPC_UNKNOWN_ERROR;
	}

	slsi_lpc_manager.lpc_dev->type = ARPHRD_IEEE80211_RADIOTAP;
	dev_change_flags(slsi_lpc_manager.lpc_dev, slsi_lpc_manager.lpc_dev->flags | IFF_UP, NULL);
	SLSI_INFO_NODEV("Create new LPC dev name=%s\n",slsi_lpc_manager.lpc_dev->name);

	ret = slsi_lpc_set_mib_local_packet_capture_mode(sdev, true);
	if (ret) {
		SLSI_ERR_NODEV("Local Packet Capture start failure: Mib setting failure ret:%d\n", ret);
		unregister_netdevice(slsi_lpc_manager.lpc_dev);
		free_netdev(slsi_lpc_manager.lpc_dev);
		slsi_lpc_manager.lpc_dev = NULL;
		return LPC_UNKNOWN_ERROR;
	}

	slsi_lpc_manager.is_mgmt_type = (type & BIT(0)) ? true : false;
	slsi_lpc_manager.is_data_type = (type & BIT(1)) ? true : false;
	slsi_lpc_manager.is_ctrl_type = (type & BIT(2)) ? true : false;

	SLSI_INFO_NODEV("Local packet Capture is started type:%d\n", type);
	slsi_lpc_manager.enable = 1;
	ampdu_agg_idx = 0;
	slsi_lpc_manager.sta_dev = sdev->netdev[SLSI_NET_INDEX_WLAN];

	return LPC_SUCCESS;
}

int slsi_lpc_stop(struct slsi_dev *sdev, const char *lpc_name, bool force)
{
	int ret = LPC_UNKNOWN_ERROR;
	int i;
	void *log_buffer = NULL;

	if (!slsi_lpc_manager.enable) {
		SLSI_INFO_NODEV("Local Packet Capture is already stopped.\n");
		return LPC_STOP_CAPTURE_IS_ALREADY_STOPPED;
	}

	slsi_lpc_manager.enable = 0;

	if(slsi_lpc_manager.lpc_dev) {
		if (!force && strcmp(slsi_lpc_manager.lpc_dev->name, lpc_name)) {
			SLSI_ERR_NODEV("The interface name(%s) doesn't match that already exists(%s).\n", lpc_name, slsi_lpc_manager.lpc_dev->name);
			slsi_lpc_manager.enable = 1;
			return LPC_UNSUPPORTED_COMMAND;
		}
		unregister_netdevice(slsi_lpc_manager.lpc_dev);
		free_netdev(slsi_lpc_manager.lpc_dev);
		SLSI_INFO_NODEV("Remove LPC dev name=%s\n", slsi_lpc_manager.lpc_dev->name);
		slsi_lpc_manager.lpc_dev = NULL;
	}

	ret = slsi_lpc_set_mib_local_packet_capture_mode(sdev, false);
	if (ret) {
		SLSI_ERR_NODEV("Local Packet Capture stop failure: Mib setting failure ret:%d\n", ret);
		return LPC_UNKNOWN_ERROR;
	}

	log_buffer = scsc_service_mxlogger_buff(sdev->service);
	if (log_buffer) {
		struct tlv_container *tlv_con;
		log_buffer += sizeof(struct log_descriptor);
		tlv_con = (struct tlv_container *)log_buffer;
		if (tlv_con->magic_number == LPC_TLV_CONTAINER_MAGIC_NUMBER) {
			tlv_con->magic_number = 0;
			tlv_con->r_index = 0;
		} else {
			SLSI_ERR_NODEV("Wrong tlv container\n");
		}
		log_buffer += sizeof(struct tlv_container);
		log_buffer += tlv_con->buffer_size;
		tlv_con = (struct tlv_container *)log_buffer;
		if (tlv_con->magic_number == LPC_TLV_CONTAINER_MAGIC_NUMBER_FW) {
			tlv_con->r_index = 0;
			tlv_con->w_index = 0;
			tlv_con->of_tlvs = 0;
		} else {
			SLSI_ERR_NODEV("Wrong tlv fw container\n");
		}
	} else {
		SLSI_ERR_NODEV("Can't get TLV Container\n");
	}

	spin_lock_bh(&slsi_lpc_manager.lpc_queue_lock);
	__skb_queue_purge(&slsi_lpc_manager.queue);
	spin_unlock_bh(&slsi_lpc_manager.lpc_queue_lock);
	flush_work(&slsi_lpc_manager.lpc_work);

	spin_lock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
	for (i = 0; i < LPC_TABLE_SIZE; i++)
		slsi_lpc_clear_packet_data(&slsi_lpc_manager.lpc_rx_buffer_table[i]);

	spin_unlock_bh(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);

	spin_lock_bh(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);
	for (i = 0; i < LPC_TABLE_SIZE; i++)
		slsi_lpc_clear_packet_data(&slsi_lpc_manager.lpc_tx_buffer_table[i]);

	spin_unlock_bh(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);

	spin_lock_bh(&slsi_lpc_manager.lpc_ampdu_info_lock);
	for (i = 0; i < LPC_TABLE_SIZE; i++)
		slsi_lpc_clear_packet_data(&slsi_lpc_manager.lpc_rx_ampdu_info_table[i]);

	spin_unlock_bh(&slsi_lpc_manager.lpc_ampdu_info_lock);

	SLSI_INFO_NODEV("Local packet Capture is Stopped.\n");

	return LPC_SUCCESS;
}

int slsi_lpc_init(void)
{
	int ret = 0;
	int i;

	INIT_WORK(&slsi_lpc_manager.lpc_work, slsi_lpc_work_func);
	skb_queue_head_init(&slsi_lpc_manager.queue);
	spin_lock_init(&slsi_lpc_manager.lpc_queue_lock);
	spin_lock_init(&slsi_lpc_manager.lpc_ampdu_info_lock);
	spin_lock_init(&slsi_lpc_manager.lpc_tx_skb_buffer_lock);
	spin_lock_init(&slsi_lpc_manager.lpc_rx_skb_buffer_lock);
	for (i = 0; i < LPC_TABLE_SIZE; i++) {
		INIT_LIST_HEAD(&slsi_lpc_manager.lpc_tx_buffer_table[i]);
		INIT_LIST_HEAD(&slsi_lpc_manager.lpc_rx_buffer_table[i]);
		INIT_LIST_HEAD(&slsi_lpc_manager.lpc_rx_ampdu_info_table[i]);
	}

	return ret;
}

int slsi_lpc_deinit(struct slsi_dev *sdev)
{
	return slsi_lpc_stop(sdev, "", true);
}

int slsi_lpc_is_lpc_enabled(void)
{
	return slsi_lpc_manager.enable;
}

int slsi_lpc_is_lpc_enabled_by_name(const char * lpc_name)
{
	if (!slsi_lpc_manager.lpc_dev) {
		return slsi_lpc_manager.enable;
	}

	if (strcmp(slsi_lpc_manager.lpc_dev->name, lpc_name)) {
		return LPC_UNSUPPORTED_COMMAND;
	}

	return slsi_lpc_manager.enable;
}
