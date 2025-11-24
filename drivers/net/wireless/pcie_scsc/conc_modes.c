/*****************************************************************************
 *
 * Copyright (c) 2014 - 2024 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/
#include <linux/version.h>

#include "dev.h"
#include "cfg80211_ops.h"
#include "mgt.h"
#include "netif.h"
#include "nl80211_vendor.h"

#ifdef CONFIG_SCSC_BB_REDWOOD
#define MAX_IFACE_LIMITS 8
#define MAX_IFACE_COMBINATIONS 16

typedef struct {
	/* Max number of interfaces of same type */
	u32 max_limit;
	/* BIT mask of interfaces from wifi_interface_type */
	u32 iface_mask;
} wifi_iface_limit;

typedef struct {
	/* Maximum number of concurrent interfaces allowed in this combination */
	u32 max_ifaces;
	/* Total number of interface limits in a combination */
	u32 num_iface_limits;
	/* Interface limits */
	wifi_iface_limit iface_limits[MAX_IFACE_LIMITS];
} wifi_iface_combination;

typedef struct {
	/* Total count of possible iface combinations */
	u32 num_iface_combinations;
	/* Interface combinations */
	wifi_iface_combination iface_combinations[MAX_IFACE_COMBINATIONS];
} wifi_iface_concurrency_matrix;

static const wifi_iface_concurrency_matrix iface_concurrency_matrix = {
	/* Increment below number by one when adding a new interface combination to the matrix*/
        .num_iface_combinations = 3,
        .iface_combinations = {
	/* STA + (DUAL_AP/NAN/STA/AP/P2P) */
	{
		.max_ifaces = 2,
		.num_iface_limits = 2,
		.iface_limits = {
			{
			.max_limit = 1,
			.iface_mask = BIT(WIFI_INTERFACE_TYPE_STA)
			},
			{
			.max_limit = 1,
#ifdef CONFIG_SLSI_WLAN_STA_W_BRIDGEDAP
			.iface_mask = BIT(WIFI_INTERFACE_TYPE_AP_BRIDGED) | BIT(WIFI_INTERFACE_TYPE_NAN) |
				      BIT(WIFI_INTERFACE_TYPE_STA) | BIT(WIFI_INTERFACE_TYPE_AP) |
				      BIT(WIFI_INTERFACE_TYPE_P2P)
#else
			.iface_mask = BIT(WIFI_INTERFACE_TYPE_NAN) | BIT(WIFI_INTERFACE_TYPE_STA) |
				      BIT(WIFI_INTERFACE_TYPE_AP) | BIT(WIFI_INTERFACE_TYPE_P2P)
#endif
			},
		}
	},
	/* STA + NAN + AP/P2P */
	{
		.max_ifaces = 3,
		.num_iface_limits = 3,
		.iface_limits = {
			{
			.max_limit = 1,
			.iface_mask = BIT(WIFI_INTERFACE_TYPE_STA)
			},
			{
			.max_limit = 1,
			.iface_mask = BIT(WIFI_INTERFACE_TYPE_NAN)
			},
			{
			.max_limit = 1,
			.iface_mask = BIT(WIFI_INTERFACE_TYPE_P2P) | BIT(WIFI_INTERFACE_TYPE_AP)
			},
		},
	},

	/* DUAL_AP */
	{
		.max_ifaces = 1,
		.num_iface_limits = 1,
		.iface_limits = {
			{
			.max_limit = 1,
			.iface_mask = BIT(WIFI_INTERFACE_TYPE_AP_BRIDGED)
			},
		},
	},
	},

};

int slsi_get_concurrency_matrix(struct wiphy *wiphy,
				struct wireless_dev *wdev, const void *data, int len)
{
	int i = 0, j = 0, ret = 0;
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct sk_buff *reply;
	struct nlattr *nlattr_nested_0, *nlattr_nested_1;

	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(iface_concurrency_matrix));
	if (!reply) {
		SLSI_ERR(sdev, "Error when allocating response buffer\n");
		ret = -ENOMEM;
		return ret;
	}

	ret |= nla_put_u32(reply, SLSI_ATTRIBUTE_NUM_IFACE_COMB,
			   iface_concurrency_matrix.num_iface_combinations);

	for(i = 0; i < iface_concurrency_matrix.num_iface_combinations; i++) {
		nlattr_nested_0 = nla_nest_start(reply, SLSI_ATTRIBUTE_IFACE_COMB);
		if (!nlattr_nested_0) {
			SLSI_ERR(sdev, "Error in nla_nest_start 0\n");
			kfree_skb(reply);
			return -1;
		}

		ret |= nla_put_u32(reply, SLSI_ATTRIBUTE_MAX_IFACE,
				   iface_concurrency_matrix.iface_combinations[i].max_ifaces);
		ret |= nla_put_u32(reply, SLSI_ATTRIBUTE_NUM_IFACE_LIMITS,
				   iface_concurrency_matrix.iface_combinations[i].num_iface_limits);

		for (j = 0; j < iface_concurrency_matrix.iface_combinations[i].num_iface_limits; j++) {
			nlattr_nested_1 = nla_nest_start(reply, SLSI_ATTRIBUTE_WIFI_IFACE_LIMIT);
			if (!nlattr_nested_1) {
				SLSI_ERR(sdev, "Error in nla_nest_start 1\n");
				kfree_skb(reply);
				return -1;
			}

			ret |= nla_put_u32(reply, SLSI_ATTRIBUTE_MAX_LIMIT,
					   iface_concurrency_matrix.iface_combinations[i].iface_limits[j].max_limit);
			ret |= nla_put_u32(reply, SLSI_ATTRIBUTE_IFACE_MASK,
					   iface_concurrency_matrix.iface_combinations[i].iface_limits[j].iface_mask);

			nla_nest_end(reply, nlattr_nested_1);
		}
		nla_nest_end(reply, nlattr_nested_0);
	}

	if (ret) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", ret);
		kfree_skb(reply);
		return ret;
	}
	ret =  cfg80211_vendor_cmd_reply(reply);

	if (ret)
		SLSI_ERR(sdev, "FAILED to reply GET_CONCURRENCY_MATRIX ret = %d\n", ret);

	return ret;
}

#else
int slsi_get_concurrency_matrix(struct wiphy *wiphy,
				       struct wireless_dev *wdev, const void *data, int len)
{
	SLSI_ERR_NODEV("This SoC doesn't support this feature\n");
	return -EOPNOTSUPP;
}
#endif
