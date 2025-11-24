/******************************************************************************
 *                                                                            *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * Bluetooth HCI Property                                                     *
 *                                                                            *
 ******************************************************************************/
#ifndef __SLSI_BT_PROPERTY_H__
#define __SLSI_BT_PROPERTY_H__
#include <linux/skbuff.h>
#include "hci_trans.h"

enum {
	SLSI_BTP_VS_CHANNEL_UNKNOWN = 0,
	SLSI_BTP_VS_CHANNEL_MXLOG = 1,
	// SLSI_BTP_VS_CHANNEL_FASTPATH = 2,  /* This is handled in H5 */
	SLSI_BTP_VS_CHANNEL_LR_WPAN = 3,
};

int slsi_bt_property_init(struct hci_trans *htr);

int slsi_bt_property_set_logmask(struct hci_trans *htr,
		const unsigned int *val, const unsigned char len);

#endif /* __SLSI_BT_PROPERTY_H__ */
