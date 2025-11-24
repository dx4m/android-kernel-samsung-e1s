/*****************************************************************************
 *
 * Copyright (c) 2012 - 2022 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/
#ifndef __SLSI_LOG2US_H__
#define __SLSI_LOG2US_H__
#include <linux/types.h>
#include <linux/ratelimit.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>

#include "mib.h"
#include "dev.h"

struct slsi_dev;
#define SLSI_LOG2US_BURST 20
#define BUFF_SIZE 256

enum slsi_sta_mlo_ls_event {
	SLSI_STA_MLO_LS_EVENT_HIGH_BMISS,
	SLSI_STA_MLO_LS_EVENT_LESS_BMISS,
	SLSI_STA_MLO_LS_EVENT_HIGH_BRSSI,
	SLSI_STA_MLO_LS_EVENT_LESS_BRSSI,
	SLSI_STA_MLO_LS_EVENT_HIGH_CONGESTION,
	SLSI_STA_MLO_LS_EVENT_LESS_CONGESTION,
	SLSI_STA_MLO_LS_EVENT_HIGH_THRPUT,
	SLSI_STA_MLO_LS_EVENT_LESS_THRPUT,
	SLSI_STA_MLO_LS_EVENT_LOW_LTNCY_DISABLE,
	SLSI_STA_MLO_LS_EVENT_LOW_LTNCY_ENABLE,
	SLSI_STA_MLO_LS_SAME_TXOP_UL_DL ,
	SLSI_STA_MLO_LS_COEX_IN_USE,
	SLSI_STA_MLO_LS_DISABLE_LINK,
	SLSI_STA_MLO_LS_ENABLE_LINK,
	SLSI_STA_MLO_LS_ACTIVE_LINK,
	SLSI_STA_MLO_LS_INACTIVE_LINK,
	SLSI_STA_MLO_LS_FORCED_ACTIVE_LINK,
	SLSI_STA_MLO_LS_FORCED_INACTIVE_LINK,
};

void slsi_conn_log2us_init(struct slsi_dev *sdev);
void slsi_conn_log2us_deinit(struct slsi_dev *sdev);
void slsi_conn_log2us_connecting(struct slsi_dev *sdev, struct net_device *dev, struct cfg80211_connect_params *sme);
void slsi_conn_log2us_connect_sta_info(struct slsi_dev *sdev, struct net_device *dev);

void slsi_conn_log2us_connecting_fail(struct slsi_dev *sdev, struct net_device *dev,
				      const unsigned char *bssid,
				      int freq, int reason);
void slsi_conn_log2us_disconnect(struct slsi_dev *sdev, struct net_device *dev,
				 const unsigned char *bssid, int reason);
void slsi_conn_log2us_eapol_gtk(struct slsi_dev *sdev, struct net_device *dev, int eapol_msg_type, u8 mlo_band);
void slsi_conn_log2us_eapol_ptk(struct slsi_dev *sdev, struct net_device *dev, int eapol_msg_type, u8 mlo_band);
void slsi_conn_log2us_roam_scan_start(struct slsi_dev *sdev, struct net_device *dev, int reason,
				      int roam_rssi_val, short chan_utilisation, int rssi_thresh, u64 timestamp);
void slsi_conn_log2us_roam_result(struct slsi_dev *sdev, struct net_device *dev,
				  char *bssid, u64 timestamp, bool roam_candidate);
void slsi_conn_log2us_eap(struct slsi_dev *sdev, struct net_device *dev, u8 *eap_type, u8 mlo_band);
void slsi_conn_log2us_dhcp(struct slsi_dev *sdev, struct net_device *dev, char *str, u8 mlo_band);
void slsi_conn_log2us_dhcp_tx(struct slsi_dev *sdev, struct net_device *dev, char *tx_status, u8 mlo_band,
			      char *dhcp_type_str);
void slsi_conn_log2us_eap_with_len(struct slsi_dev *sdev, struct net_device *dev,
				   u8 *eap_type, int eap_length, u8 mlo_band);
void slsi_conn_log2us_auth_req(struct slsi_dev *sdev, struct net_device *dev,
			       const unsigned char *bssid,
			       int auth_algo, int sae_type, int sn,
			       int status, u32 tx_status, int is_roaming, short rssi);

void slsi_conn_log2us_auth_resp(struct slsi_dev *sdev, struct net_device *dev,
				const unsigned char *bssid,
				int auth_algo,
				int sae_type,
				int sn, int status, int is_roaming);
void slsi_conn_log2us_assoc_req(struct slsi_dev *sdev, struct net_device *dev,
				const unsigned char *bssid,
				int sn, int tx_status, int mgmt_frame_subtype, u8 mlo_band, short rssi);
void slsi_conn_log2us_assoc_resp(struct slsi_dev *sdev, struct net_device *dev,
				 const unsigned char *bssid, int sn, int status,
				 int mgmt_frame_subtype, int aid, const unsigned char *mld_addr);

void slsi_conn_log2us_deauth(struct slsi_dev *sdev, struct net_device *dev, char *str_type,
			     const unsigned char *bssid, int sn, int status, char *vs_ie, short rssi);

void slsi_conn_log2us_disassoc(struct slsi_dev *sdev, struct net_device *dev, char *str_type,
			       const unsigned char *bssid, int sn, int status, char *vs_ie, short rssi);
void slsi_conn_log2us_roam_scan_done(struct slsi_dev *sdev, struct net_device *dev, u64 timestamp);
void slsi_conn_log2us_roam_scan_result(struct slsi_dev *sdev, struct net_device *dev, bool candidate,
				       char *bssid, int freq,
				       int rssi, short cu,
				       int score, int tp_score, bool eligible, bool mld_ap);
void slsi_conn_log2us_btm_query(struct slsi_dev *sdev, struct net_device *dev,
				int dialog, int reason, u8 mlo_band);
void slsi_conn_log2us_btm_req(struct slsi_dev *sdev, struct net_device *dev,
			      int dialog, int btm_mode,
			      int disassoc_timer,
			      int validity_time, int candidate_count, u8 mlo_band);
void slsi_conn_log2us_btm_resp(struct slsi_dev *sdev, struct net_device *dev,
			       int dialog,
			       int btm_mode, int delay, char *bssid, u8 mlo_band);
u8 *get_eap_type_from_val(int val, u8 *str);
void slsi_conn_log2us_eapol_tx(struct slsi_dev *sdev, struct net_device *dev, u32 status_code, u8 mlo_band);
void slsi_conn_log2us_eapol_ptk_tx(struct slsi_dev *sdev, u32 status_code, u8 mlo_band);
void slsi_conn_log2us_eapol_gtk_tx(struct slsi_dev *sdev, u32 status_code, u8 mlo_band);
void slsi_conn_log2us_eap_tx(struct slsi_dev *sdev, struct netdev_vif *ndev_vif,
			     int eap_length, int eap_type, char *tx_status_str, u8 mlo_band);
void slsi_conn_log2us_btm_cand(struct slsi_dev *sdev, struct net_device *dev,
			       char *bssid, int prefer);
void slsi_conn_log2us_btm_wtc(struct slsi_dev *sdev, struct net_device *dev,
			      u8 reason, u8 subreason, u32 duration);
void slsi_conn_log2us_btm_wtc_sent(struct slsi_dev *sdev, struct net_device *dev,
				   u8 reason_code);
void slsi_log2us_handle_frame_tx_status(struct slsi_dev *sdev, struct net_device *dev,
					u16 host_tag, u16 tx_status, u8 mlo_band);
void slsi_conn_log2us_roam_scan_save(struct slsi_dev *sdev, struct net_device *dev, int scan_type,
				     int freq_count, int *freq_list);
void slsi_conn_log2us_nr_frame_req(struct slsi_dev *sdev, struct net_device *dev, int dialog_token, char *ssid,
				   u8 mlo_band);
void slsi_conn_log2us_nr_frame_resp(struct slsi_dev *sdev, struct net_device *dev,  int dialog_token, int freq_count,
				    int *freq_list, int report_number, u8 mlo_band);
void slsi_conn_log2us_beacon_report_request(struct slsi_dev *sdev, struct net_device *dev,
					    int dialog_token, int operating_class, char *string,
					    int measure_duration, char *measure_mode, u8 request_mode, u8 mlo_band);
void slsi_conn_log2us_beacon_report_response(struct slsi_dev *sdev, struct net_device *dev, int dialog_token,
					     int report_number, u8 mlo_band);

void slsi_conn_log2us_ncho_mode(struct slsi_dev *sdev, struct net_device *dev, int enable);
void slsi_conn_log2us_roam_cancelled(struct slsi_dev *sdev, struct net_device *dev, int reason_code, char *reason_desc);
#ifdef CONFIG_SCSC_WLAN_EHT
void slsi_conn_log2us_mld_setup(struct slsi_dev *sdev, struct net_device *dev, int band,
			       char *bssid, int status_code, int link_id);
void slsi_conn_log2us_mld_reconfig(struct slsi_dev *sdev, struct net_device *dev, int band, int link_id);
void slsi_conn_log2us_mld_t2lm_status(struct slsi_dev *sdev, struct net_device *dev, int band, int tid_dl, int tid_ul);
void slsi_conn_log2us_mld_t2lm_req_rsp(struct slsi_dev *sdev, struct net_device *dev, int t2lm_ftype, int mlo_band,
				       int dialog_token, int status_code, int tx_status);
void slsi_conn_log2us_mld_t2lm_teardown(struct slsi_dev *sdev, struct net_device *dev, int mlo_band, int tx_status);
void slsi_conn_log2us_mld_link(struct slsi_dev *sdev, struct net_device *dev, int active_band, int inactive_band,
			       u8 reason);
#endif
#endif
