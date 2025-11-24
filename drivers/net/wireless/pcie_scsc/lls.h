/*****************************************************************************
 *
 * Copyright (c) 2012 - 2023 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef __SLSI_LLS_H_
#define __SLSI_LLS_H_

#include "mib.h"

/* Take largest value of same band radios (radio 1 & radio 2) and assign it to radio1.
 * Assign 0 to radio2.
 */
#define SLSI_RADIO_STATS_REASSIGN(a, b, val) \
	do { \
		a->val = (a->val > b->val) ? a->val : b->val; \
		b->val = 0; \
	} while(0)

/* channel information */
struct slsi_lls_channel_info {
	enum slsi_lls_channel_width width;   /* channel width (20, 40, 80, 80+80, 160)*/
	int center_freq;   /* primary 20 MHz channel */
	int center_freq0;  /* center frequency (MHz) first segment */
	int center_freq1;  /* center frequency (MHz) second segment */
};

/* channel statistics */
struct slsi_lls_channel_stat {
	struct slsi_lls_channel_info channel;
	u32 on_time;                /* msecs the radio is awake (32 bits number accruing over time) */
	u32 cca_busy_time;          /* msecs the CCA register is busy (32 bits number accruing over time) */
};

/* wifi rate */
struct slsi_lls_rate {
	u32 preamble   :3;   /* 0: OFDM, 1:CCK, 2:HT 3:VHT 4:HE 5:EHT 6..7 reserved*/
	u32 nss        :2;   /* 0:1x1, 1:2x2, 3:3x3, 4:4x4*/
	u32 bw         :3;   /* 0:20MHz, 1:40Mhz, 2:80Mhz, 3:160Mhz, 4:320Mhz*/
	u32 rate_mcs_idx :8; /* OFDM/CCK rate code would be as per ieee std in the units of 0.5mbps
			      * HT/VHT/HE/EHT it would be mcs index
			      */
	u32 reserved  :16;   /* reserved*/
	u32 bitrate;         /* units of 100 Kbps*/
};

/* per rate statistics */
struct slsi_lls_rate_stat {
	struct slsi_lls_rate rate;     /* rate information*/
	u32 tx_mpdu;        /* number of successfully transmitted data pkts (ACK rcvd)*/
	u32 rx_mpdu;        /* number of received data pkts*/
	u32 mpdu_lost;      /* number of data packet losses (no ACK)*/
	u32 retries;        /* total number of data pkt retries*/
	u32 retries_short;  /* number of short data pkt retries*/
	u32 retries_long;   /* number of long data pkt retries*/
};

/* radio statistics */
struct slsi_lls_radio_stat {
	int radio;               /* wifi radio (if multiple radio supported)*/
	u32 on_time;                    /* msecs the radio is awake (32 bits number accruing over time)*/
	u32 tx_time;                    /* msecs the radio is transmitting (32 bits number accruing over time)*/
	u32 rx_time;                    /* msecs the radio is in active receive (32 bits number accruing over time)*/
	u32 on_time_scan;               /* msecs the radio is awake due to all scan (32 bits number accruing over time)*/
	u32 on_time_nbd;                /* msecs the radio is awake due to NAN (32 bits number accruing over time)*/
	u32 on_time_gscan;              /* msecs the radio is awake due to G?scan (32 bits number accruing over time)*/
	u32 on_time_roam_scan;          /* msecs the radio is awake due to roam?scan (32 bits number accruing over time)*/
	u32 on_time_pno_scan;           /* msecs the radio is awake due to PNO scan (32 bits number accruing over time)*/
	u32 on_time_hs20;               /* msecs the radio is awake due to HS2.0 scans and GAS exchange (32 bits number accruing over time)*/
	u32 num_channels;               /* number of channels*/
	struct slsi_lls_channel_stat channels[];   /* channel statistics*/
};

struct slsi_lls_interface_link_layer_info {
	enum slsi_lls_interface_mode mode;     /* interface mode*/
	u8   mac_addr[6];                  /* interface mac address (self)*/
	enum slsi_lls_connection_state state;  /* connection state (valid for STA, CLI only)*/
	enum slsi_lls_roam_state roaming;      /* roaming state*/
	u32 capabilities;                  /* WIFI_CAPABILITY_XXX (self)*/
	u8 ssid[33];                       /* null terminated SSID*/
	u8 bssid[6];                       /* bssid*/
	u8 ap_country_str[3];              /* country string advertised by AP*/
	u8 country_str[3];                 /* country string for this association*/
	u8 time_slicing_duty_cycle_percent;/* if this iface is being served using time slicing
					    * on a radio with one or more ifaces (i.e MCC),
					    * then the duty cycle assigned to this iface in %.
					    * If not using time slicing (i.e SCC or DBS), set to 100.
					    */
};

/* per peer statistics */
typedef struct bssload_info {
	u16 sta_count;                          /* station count*/
	u16 chan_util;                          /* channel utilization*/
	u8 PAD[4];
} bssload_info_t;

/* per peer statistics */
struct slsi_lls_peer_info {
	enum slsi_lls_peer_type type;           /* peer type (AP, TDLS, GO etc.)*/
	u8 peer_mac_address[6];                 /* mac address*/
	u32 capabilities;                       /* peer WIFI_CAPABILITY_XXX*/
	bssload_info_t bssload;                 /* STA count and CU*/
	u32 num_rate;                           /* number of rates*/
	struct slsi_lls_rate_stat rate_stats[]; /* per rate statistics, number of entries  = num_rate*/
};

/* Per access category statistics */
struct slsi_lls_wmm_ac_stat {
	enum slsi_lls_traffic_ac ac;  /* access category (VI, VO, BE, BK)*/
	u32 tx_mpdu;                  /* number of successfully transmitted unicast data pkts (ACK rcvd)*/
	u32 rx_mpdu;                  /* number of received unicast data packets*/
	u32 tx_mcast;                 /* number of successfully transmitted multicast data packets*/
	u32 rx_mcast;                 /* number of received multicast data packets*/
	u32 rx_ampdu;                 /* number of received unicast a-mpdus; support of this counter is optional*/
	u32 tx_ampdu;                 /* number of transmitted unicast a-mpdus; support of this counter is optional*/
	u32 mpdu_lost;                /* number of data pkt losses (no ACK)*/
	u32 retries;                  /* total number of data pkt retries*/
	u32 retries_short;            /* number of short data pkt retries*/
	u32 retries_long;             /* number of long data pkt retries*/
	u32 contention_time_min;      /* data pkt min contention time (usecs)*/
	u32 contention_time_max;      /* data pkt max contention time (usecs)*/
	u32 contention_time_avg;      /* data pkt avg contention time (usecs)*/
	u32 contention_num_samples;   /* num of data pkts used for contention statistics*/
};

/* interface statistics */
struct slsi_lls_iface_stat {
	void *iface;                                     /* wifi interface*/
	struct slsi_lls_interface_link_layer_info info;  /* current state of the interface*/
	u32 beacon_rx;                                   /* access point beacon received count from connected AP*/
	u64 average_tsf_offset;                          /* average beacon offset encountered (beacon_TSF - TBTT)*/
	u32 leaky_ap_detected;                           /* indicate that this AP typically leaks packets beyond the driver guard time.*/
	u32 leaky_ap_avg_num_frames_leaked;              /* average number of frame leaked by AP after frame with PM bit set was ACK'ed by AP*/
	u32 leaky_ap_guard_time;
	u32 mgmt_rx;                                     /* access point mgmt frames received count from connected AP (including Beacon)*/
	u32 mgmt_action_rx;                              /* action frames received count*/
	u32 mgmt_action_tx;                              /* action frames transmit count*/
	int rssi_mgmt;                                   /* access Point Beacon and Management frames RSSI (averaged)*/
	int rssi_data;                                   /* access Point Data Frames RSSI (averaged) from connected AP*/
	int rssi_ack;                                    /* access Point ACK RSSI (averaged) from connected AP*/
	struct slsi_lls_wmm_ac_stat ac[SLSI_LLS_AC_MAX]; /* per ac data packet statistics*/
	u32 num_peers;                                   /* number of peers*/
	struct slsi_lls_peer_info peer_info[];           /* per peer statistics*/
};

/* various states for the link */
enum slsi_lls_link_state {
	/* chip does not support reporting the state of the link.*/
	WIFI_LINK_STATE_UNKNOWN = 0,
	/* link has not been in use since last report. It is placed in power save. all
	 * management, control and data frames for the MLO connection are carried over
	 * other links. in this state the link will not listen to beacons even in DTIM
	 * period and does not perform any GTK/IGTK/BIGTK updates but remains
	 * associated.
	 */
	WIFI_LINK_STATE_NOT_IN_USE = 1,
	/* link is in use. In presence of traffic, it is set to be power active. when
	 * the traffic stops, the link will go into power save mode and will listen
	 * for beacons every DTIM period.
	 */
	WIFI_LINK_STATE_IN_USE = 2,
};

/* per link statistics */
struct slsi_lls_link_stat {
	u8 link_id;                            /* identifier for the link */
	enum slsi_lls_link_state state;        /* state for the link */
	int radio;                             /* radio on which link stats are sampled */
	u32 frequency;                         /* frequency on which link is operating */

	u32 beacon_rx;                         /* beacon received count from connected AP on the link*/
	u64 average_tsf_offset;                /* average beacon offset encountered (beacon_TSF - TBTT)
						* the average_tsf_offset field is used so as to calculate
						* the typical beacon contention time on the channel as well
						* may be used to debug beacon synchronization and related
						* power consumption issue
						*/
	u32 leaky_ap_detected;                 /* indicate that this AP typically leaks packets
						* beyond the driver guard time.
						*/
	u32 leaky_ap_avg_num_frames_leaked;    /* average number of frame leaked by AP after frame
						* with PM bit set was ACK'ed by AP
						*/

	u32 leaky_ap_guard_time;	       /* guard time currently in force (when implementing
						* IEEE power management based on frame control PM
						* bit), how long driver waits before shutting down
						* the radio and after receiving an ACK for a data
						* frame with PM bit set)
						*/
	u32 mgmt_rx;                           /* mgmt frames received count from connected AP on the link (including Beacon)*/
	u32 mgmt_action_rx;                    /* action frames received count on the link*/
	u32 mgmt_action_tx;                    /* action frames transmit count on the link*/
	int rssi_mgmt;                         /* access Point Beacon and Management frames RSSI (averaged) on the link*/
	int rssi_data;                         /* access Point Data Frames RSSI (averaged) from connected AP on the link*/
	int rssi_ack;                          /* access Point ACK RSSI (averaged) from connected AP on the links*/

	struct slsi_lls_wmm_ac_stat ac[SLSI_LLS_AC_MAX];     /* per ac data packet statistics for the link*/
	u8 time_slicing_duty_cycle_percent;    /* if this link is being served using
						* time slicing on a radio with one or
						* more links, then the duty cycle
						* assigned to this link in %
						*/
	u32 num_peers;                         /* number of peers*/
	struct slsi_lls_peer_info peer_info[]; /* per peer statistics fot the link*/
};

/* multi link stats for interface  */
struct slsi_lls_iface_ml_stat {
	void *iface;                                     /* wifi interface */
	struct slsi_lls_interface_link_layer_info info;  /* current state of the interface*/
	int num_links;                                   /* number of links */
	struct slsi_lls_link_stat links[];               /* stats per link */
};

struct slsi_lls_iface_data {
	int radio;
	u32 frequency;
	u32 beacon_rx;
	u64 average_tsf_offset;
	u32 leaky_ap_detected;
	u32 leaky_ap_avg_num_frames_leaked;
	u32 leaky_ap_guard_time;
	u32 mgmt_rx;
	u32 mgmt_action_rx;
	u32 mgmt_action_tx;
	int rssi_mgmt;
	int rssi_data;
	int rssi_ack;
	struct slsi_lls_wmm_ac_stat ac[SLSI_LLS_AC_MAX];
	u8 time_slicing_duty_cycle_percent;
};

void slsi_lls_start_stats(struct slsi_dev *sdev, u32 mpdu_size_threshold, u32 aggr_stat_gathering);
int slsi_lls_fill_stats(struct slsi_dev *sdev, u8 **src_buf, bool is_mlo);
void slsi_lls_stop_stats(struct slsi_dev *sdev, u32 stats_clear_req_mask);
int slsi_lls_update_stats(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);

static inline enum slsi_lls_traffic_ac slsi_fapi_to_android_traffic_q(enum slsi_traffic_q fapi_q)
{
	switch (fapi_q) {
	case SLSI_TRAFFIC_Q_BE:
		return SLSI_LLS_AC_BE;
	case SLSI_TRAFFIC_Q_BK:
		return SLSI_LLS_AC_BK;
	case SLSI_TRAFFIC_Q_VI:
		return SLSI_LLS_AC_VI;
	case SLSI_TRAFFIC_Q_VO:
		return SLSI_LLS_AC_VO;
	default:
		return SLSI_LLS_AC_MAX;
	}
}
#endif
