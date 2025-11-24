/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************
 *
 * Copyright (c) 2012 - 2025 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef __DCTAS_H__
#define __DCTAS_H__

#include "osal/slsi_wakelock.h"

#define PCIE_LINK_STATE_OFF	    (0)
#define PCIE_LINK_STATE_ON	    (1)
#define PCIE_LINK_STATE_ERROR	(2)

#define INTERVAL_CP_READ_MS     (250)
#define TIMEOUT_PETERSON_LOCK	(25)

#define OFFSET_PETERSON_FLAG_CP     (0x0000)
#define OFFSET_PETERSON_FLAG_WLBT   (0x0004)
#define OFFSET_PETERSON_TURN        (0x0008)

#define PETERSON_TURN_CP	(0)
#define PETERSON_TURN_WLBT	(1)

#define OFFSET_CP_STATE         (0x0010)
#define OFFSET_CP_POLICY        (0x0011)
#define OFFSET_CP_AVERAGE       (0x0012)
#define OFFSET_CP_S_UPPER       (0x0013)

#define OFFSET_WLBT_STATE       (0x2000)
#define OFFSET_WLBT_AVERAGE     (0x2001)
#define OFFSET_WLBT_OFF_TIME_MID	(0x2002)
#define OFFSET_WLBT_OFF_TIME_LOW	(0x2003)

#define DCTAS_STATE_NOT_ASSIGNED	(0x00)
#define DCTAS_STATE_UNUSED		(0x01)
#define DCTAS_STATE_USED		(0x02)

#define DCTAS_AVERAGE_LOW	(0x00)
#define DCTAS_AVERAGE_MIDLOW	(0x01)
#define DCTAS_AVERAGE_MID	(0x02)
#define DCTAS_AVERAGE_MIDHIGH	(0x03)
#define DCTAS_AVERAGE_HIGH	(0x04)
#define DCTAS_AVERAGE_OVER	(0x05)
#define DCTAS_AVERAGE_UNDERLOW	(0x99)

#define DCTAS_MAX_OFF_TIME	(120)
#define DCTAS_MIFLESS_30S	(30000)

#define CP_SHMEM_READ_U8(sdev, offset) (*((u8 *)(sdev)->dctas.cp_base_addr + (offset)))
#define CP_SHMEM_WRITE_U8(sdev, offset, v) (*((u8 *)(sdev)->dctas.cp_base_addr + (offset)) = v)

int pcie_mif_register_link_status_notifier(struct notifier_block *nb);
int pcie_mif_unregister_link_status_notifier(struct notifier_block *nb);

struct slsi_dev;

struct slsi_cp_state {
	u8 state;
	u8 policy;
	u8 average;
	u8 s_upper;
};

struct slsi_wlbt_state {
	u8 state;
	u8 average;
	u8 off_time_mid;
	u8 off_time_low;
};

struct slsi_dctas {
	bool			   dctas_initialized;

	struct slsi_wake_lock	   wlan_wl_dctas;
	struct notifier_block      link_notifier;
	int                        link_state;

	void __iomem      *cp_base_addr;
	struct slsi_cp_state       cp_cp_state;
	struct slsi_cp_state       fw_cp_state;
	struct slsi_wlbt_state     wlbt_state;

	u8			   cp_state_duration;
	bool			   update_deferred;

	struct delayed_work        read_cp_work;
	struct delayed_work	   mifless_work;
	struct work_struct	   deferred_work;
};

void init_dctas(struct slsi_dev *sdev);
void deinit_dctas(struct slsi_dev *sdev);

void dctas_wlan_on(struct slsi_dev *sdev);
void dctas_wlan_off(struct slsi_dev *sdev);
void dctas_wlan_sar_ind(struct slsi_dev *sdev, struct sk_buff *skb);

#endif /* __DCTAS_H__ */
