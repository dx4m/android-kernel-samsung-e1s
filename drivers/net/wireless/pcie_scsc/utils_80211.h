/* SPDX-License-Identifier: <SPDX License Expression> */
/*****************************************************************************
 *
 * Copyright (c) 2012 - 2024 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef __SLSI_UTILS_80211_H__
#define __SLSI_UTILS_80211_H__

#define SLSI_WLAN_AKM_SUITE_SAE_EXT_KEY       SUITE(0x000FAC, 24)
#define SLSI_WLAN_AKM_SUITE_FT_SAE_EXT_KEY    SUITE(0x000FAC, 25)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0))
#define WLAN_AKM_SUITE_OWE                    SUITE(0x000FAC, 18)
#define WLAN_AKM_SUITE_FT_PSK_SHA384            SUITE(0x000FAC, 19)
#define WLAN_AKM_SUITE_PSK_SHA384               SUITE(0x000FAC, 20)

#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
#define WLAN_AKM_SUITE_FILS_SHA256            SUITE(0x000FAC, 14)
#define WLAN_AKM_SUITE_FILS_SHA384            SUITE(0x000FAC, 15)
#define WLAN_AKM_SUITE_FT_FILS_SHA256         SUITE(0x000FAC, 16)
#define WLAN_AKM_SUITE_FT_FILS_SHA384         SUITE(0x000FAC, 17)
#endif

#define SLSI_RSN_SELECTOR(a, b, c, d) \
	                ((((u32) (a)) << 24) | (((u32) (b)) << 16) | (((u32) (c)) << 8) |  (u32) (d))

#define SLSI_RSN_AUTH_KEY_MGMT_PASN SLSI_RSN_SELECTOR(0x00, 0x0f, 0xac, 21)
#define SLSI_RSN_AUTH_KEY_MGMT_FT_802_1X_SHA384_UNRESTRICTED \
	                SLSI_RSN_SELECTOR(0x00, 0x0f, 0xac, 22)
#define SLSI_RSN_AUTH_KEY_MGMT_802_1X_SHA384 SLSI_RSN_SELECTOR(0x00, 0x0f, 0xac, 23)

#define SLSI_RSNE_OVERRIDE_IE_VENDOR_TYPE 0x29
#define SLSI_RSNE_OVERRIDE_2_IE_VENDOR_TYPE 0x2a
#define SLSI_RSNXE_OVERRIDE_IE_VENDOR_TYPE 0x2b
#define SLSI_RSN_SELECTION_IE_VENDOR_TYPE 0x2c
#define SLSI_RSN_SELECTION_IE_LEN 5

static inline bool slsi_is_wpa3_support(const u8 *rsnie, size_t len)
{
	int pos, akm_suite_count, rsn_len;
	int i;
	u32 akm;

	if (!rsnie)
		return false;

	if (len < 2 || len < rsnie[1])
		return false;
	/* Calculate the position of AKM suite count in RSNIE
	 * RSNIE TAG(1 byte) + length(1 byte) + version(2 byte) + Group cipher suite(4 bytes)
	 * Pairwise Cipher Suite Count(2) + pairwise suite count * 4.
	 */
	if (rsnie[1] < 10)
		return false;
	pos = 8 + 2 + ((rsnie[8] | (rsnie[9] << 8)) * 4);
	if ((pos + 2) > (rsnie[1] + 2))
		return false;

	akm_suite_count = rsnie[pos] | (rsnie[pos + 1] << 8);
	rsn_len = pos + akm_suite_count * 4;
	if (rsn_len > (rsnie[1] + 2))
		return false;

	/* Add AKM suite count in pos */
	pos += 2;
	for (i = 0; i < akm_suite_count; i++) {
		akm = __cpu_to_be32(*(u32 *)&rsnie[pos]);
		if (akm == WLAN_AKM_SUITE_SAE || akm == WLAN_AKM_SUITE_FT_OVER_SAE ||
		    akm == SLSI_WLAN_AKM_SUITE_SAE_EXT_KEY || akm == SLSI_WLAN_AKM_SUITE_FT_SAE_EXT_KEY)
			return true;
		pos += 4;
	}

	return false;
}

static inline bool slsi_is_owe_support(const u8 *rsnie, int len)
{
	int pos, akm_suite_count, rsn_len;
	int i;
	u32 akm;

	if (!rsnie)
		return false;

	if (len < 2 || len < rsnie[1])
		return false;
	/* Calculate the position of AKM suite count in RSNIE
	 * RSNIE TAG(1 byte) + length(1 byte) + version(2 byte) + Group cipher suite(4 bytes)
	 * Pairwise Cipher Suite Count(2) + pairwise suite count * 4.
	 */
	if (rsnie[1] < 10)
		return false;
	pos = 8 + 2 + ((rsnie[8] | (rsnie[9] << 8)) * 4);
	if ((pos + 2) > (rsnie[1] + 2))
		return false;

	akm_suite_count = rsnie[pos] | (rsnie[pos + 1] << 8);
	rsn_len = pos + akm_suite_count * 4;
	if (rsn_len > (rsnie[1] + 2))
		return false;

	/* Add AKM suite count in pos */
	pos += 2;
	for (i = 0; i < akm_suite_count; i++) {
		akm = __cpu_to_be32(*(u32 *)&rsnie[pos]);
		if (akm == WLAN_AKM_SUITE_OWE)
			return true;
		pos += 4;
	}
	return false;
}

static inline bool slsi_is_fils_akm(const u8 *rsnie, int len)
{
	int pos, akm_suite_count, rsn_len;
	int i;
	u32 akm;

	if (!rsnie)
		return false;

	if (len < 2 || len < rsnie[1])
		return false;
	/* Calculate the position of AKM suite count in RSNIE
	 * RSNIE TAG(1 byte) + length(1 byte) + version(2 byte) + Group cipher suite(4 bytes)
	 * Pairwise Cipher Suite Count(2) + pairwise suite count * 4.
	 */
	if (rsnie[1] < 10)
		return false;
	pos = 8 + 2 + ((rsnie[8] | (rsnie[9] << 8)) * 4);
	if ((pos + 2) > (rsnie[1] + 2))
		return false;

	akm_suite_count = rsnie[pos] | (rsnie[pos + 1] << 8);
	rsn_len = pos + akm_suite_count * 4;
	if (rsn_len > (rsnie[1] + 2))
		return false;

	/* Add AKM suite count in pos */
	pos += 2;
	for (i = 0; i < akm_suite_count; i++) {
		akm = __cpu_to_be32(*(u32 *)&rsnie[pos]);
		if (akm == WLAN_AKM_SUITE_FILS_SHA256 || akm == WLAN_AKM_SUITE_FT_FILS_SHA256 ||
			akm == WLAN_AKM_SUITE_FILS_SHA384 || akm == WLAN_AKM_SUITE_FT_FILS_SHA384)
			return true;
		pos += 4;
	}
	return false;
}

#ifdef CONFIG_SCSC_WLAN_EHT
static inline bool slsi_is_rsno_wpa3_owe_support(const u8 *rsnie, int len)
{
	int pos, akm_suite_count, rsn_len;
	int i;
	u32 akm;

	if (!rsnie)
		return false;

	if (len < 2 || len < rsnie[1])
		return false;

	/* Calculate the position of AKM suite in RSNE Override element
	 * ELEMENT ID (dd)(1 byte) + length(1 byte) + vendor oui(4 bytes)+
	 * version(2 byte) + Group cipher suite(4 bytes) +
	 * pairwise suite count(2 byte) + pairwise suite count * 4 + AKM suite count(2 byte)
	 * pos is the array index not length
	 */
	if (rsnie[1] < 12)
		return false;
	pos = 12 + 2 + ((rsnie[12] | (rsnie[13] << 8)) * 4);
	if ((pos + 2) > (rsnie[1] + 2))
		return false;

	akm_suite_count = rsnie[pos] | (rsnie[pos + 1] << 8);
	rsn_len = pos + akm_suite_count * 4;
	if (rsn_len > (rsnie[1] + 2))
		return false;

	/* Add AKM suite count in pos */
	pos += 2;
	for (i = 0; i < akm_suite_count; i++) {
		akm = __cpu_to_be32(*(u32 *)&rsnie[pos]);
		if (akm == WLAN_AKM_SUITE_SAE || akm == WLAN_AKM_SUITE_FT_OVER_SAE ||
		    akm == SLSI_WLAN_AKM_SUITE_SAE_EXT_KEY || akm == SLSI_WLAN_AKM_SUITE_FT_SAE_EXT_KEY ||
		    akm == WLAN_AKM_SUITE_OWE)
			return true;
		pos += 4;
	}

	return false;
}
#endif
#endif
