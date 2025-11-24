/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************
 *
 * Copyright (c) 2012 - 2024 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef __SLSI_RIL_NOTIFIER_H__
#define __SLSI_RIL_NOTIFIER_H__

#define CP_MAX_BAND 16

enum modem_cell_info_access_technology {
	CELLULAR_INFO_ACCESS_TECHNOLOGY_NONE = 0,
	CELLULAR_INFO_ACCESS_TECHNOLOGY_LTE  = 3,
	CELLULAR_INFO_ACCESS_TECHNOLOGY_NR5G = 7,
};

struct slsi_dev;

struct __packed cp_cell_info {
	u8 rat;
	u32 band;
	u32 channel;
	u8 connection_status;
	u32 bandwidth;
	s32 sinr;
	s32 rsrp;
	s32 rsrq;
	u8 cqi;
	u8 dl_mcs;
	s32 pusch_power;
};

struct __packed cp_noti_cell_infos {
	u32 num_cell;
	struct cp_cell_info cell_list[CP_MAX_BAND];
};

int wlbt_ril_notifier(struct notifier_block *nb, unsigned long size, void *buf);
void wlbt_register_ril_notifier(struct slsi_dev *sdev);
void wlbt_unregister_ril_notifier(struct slsi_dev *sdev);

#endif /* __SLSI_RIL_NOTIFIER_H__ */
