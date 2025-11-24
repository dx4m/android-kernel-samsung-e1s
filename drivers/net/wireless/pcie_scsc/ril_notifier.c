// SPDX-License-Identifier: GPL-2.0
/****************************************************************************
 *
 * Copyright (c) 2012 - 2023 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#include <linux/notifier.h>

#if defined(SCSC_SEP_VERSION)
#include <linux/dev_ril_bridge.h>
#else
static inline int register_dev_ril_bridge_event_notifier(struct notifier_block *nb) {return 0; }
static inline int unregister_dev_ril_bridge_event_notifier(struct notifier_block *nb) {return 0; }
static inline int dev_ril_bridge_send_msg(int id, int size, void *buf) {return 0; }

#define IPC_SYSTEM_CP_ADAPTIVE_MIPI_INFO	0x05

struct dev_ril_bridge_msg {
	unsigned int dev_id;
	unsigned int data_len;
	void *data;
};
#endif

#include "ril_notifier.h"
#include "debug.h"
#include "mlme.h"
#include "mgt.h"

static struct netdev_vif *lock_mhs_vif_and_return(struct slsi_dev *sdev)
{
	struct netdev_vif *ndev_vif = NULL;
	int i = 0;

	for (i = SLSI_NET_INDEX_P2PX_SWLAN; i <= SLSI_NET_INDEX_AP_VLAN2 ; i++) {
		if (!sdev->netdev[i])
			continue;
		ndev_vif = netdev_priv(sdev->netdev[i]);
		if (!ndev_vif)
			continue;
		SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
		if (SLSI_IS_VIF_INDEX_MHS(sdev, ndev_vif) && ndev_vif->activated)
			return ndev_vif;
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	}
	SLSI_INFO(sdev, "No AP enabled.\n");

	return NULL;
}

static void unlock_mhs_vif(struct netdev_vif *mhs_vif)
{
	if (mhs_vif)
		SLSI_MUTEX_UNLOCK(mhs_vif->vif_mutex);
}

static void filter_cell_infos(struct slsi_dev *sdev, struct cp_noti_cell_infos *cell_infos)
{
	int i, j;
	const enum modem_cell_info_access_technology coex_rat[3] = {CELLULAR_INFO_ACCESS_TECHNOLOGY_NONE,
								    CELLULAR_INFO_ACCESS_TECHNOLOGY_LTE,
								    CELLULAR_INFO_ACCESS_TECHNOLOGY_NR5G};

	memset(&sdev->cp_cell_infos, 0, sizeof(struct cp_noti_cell_infos));
	for (i = 0; i < cell_infos->num_cell; i++) {
		bool use = false;

		for (j = 0; j < ARRAY_SIZE(coex_rat); j++)
			if (cell_infos->cell_list[i].rat == coex_rat[j]) {
				use = true;
				break;
			}
		if (use) {
			memcpy(&sdev->cp_cell_infos.cell_list[sdev->cp_cell_infos.num_cell],
			       &cell_infos->cell_list[i], sizeof(struct cp_cell_info));
			sdev->cp_cell_infos.num_cell++;
		}
	}
}

int wlbt_ril_notifier(struct notifier_block *nb, unsigned long size, void *buf)
{
	struct slsi_dev *sdev = container_of(nb, struct slsi_dev, ril_notifier);
	struct netdev_vif *mhs_vif = NULL;
	struct dev_ril_bridge_msg *msg = (struct dev_ril_bridge_msg *)buf;
	struct cp_noti_cell_infos cell_infos;
	u32 msg_data_size = 0, data_size = 0, num_cell = 0;
	int i;

	if (!buf) {
		SLSI_WARN(sdev, "buf is NULL\n");
		return -EINVAL;
	}

	SLSI_INFO(sdev, "ril notification size [%ld], dev_id : %d, data_len : %d\n", size, msg->dev_id, msg->data_len);
	if (size != sizeof(struct dev_ril_bridge_msg) || msg->dev_id != IPC_SYSTEM_CP_ADAPTIVE_MIPI_INFO)
		return -EINVAL;

	msg_data_size = msg->data_len - sizeof(sdev->cp_cell_infos.num_cell);
	memcpy(&num_cell, msg->data, sizeof(sdev->cp_cell_infos.num_cell));
	SLSI_INFO(sdev, "num_cell: %d, msg_data_size : %d\n", num_cell, msg_data_size);
	data_size = sizeof(struct cp_cell_info) * num_cell;
	if (msg_data_size != data_size || num_cell > CP_MAX_BAND) {
		SLSI_ERR(sdev, "invalid num_cell : %d or mismatching msg data size : %d\n", num_cell, data_size);
		return -EINVAL;
	}

	memset(&cell_infos, 0, sizeof(struct cp_noti_cell_infos));
	memcpy(&cell_infos, msg->data, msg->data_len);

	mhs_vif = lock_mhs_vif_and_return(sdev);
	SLSI_MUTEX_LOCK(sdev->cp_coex_mutex);

	filter_cell_infos(sdev, &cell_infos);
	if (sdev->cp_cell_infos.num_cell)
		for (i = 0; i < sdev->cp_cell_infos.num_cell; i++)
			SLSI_INFO(sdev, "update cell info %d : [%d,%d,%d,%d,%d,%d]\n",
				  i, sdev->cp_cell_infos.cell_list[i].rat,
				  sdev->cp_cell_infos.cell_list[i].band,
				  sdev->cp_cell_infos.cell_list[i].channel,
				  sdev->cp_cell_infos.cell_list[i].connection_status,
				  sdev->cp_cell_infos.cell_list[i].bandwidth,
				  sdev->cp_cell_infos.cell_list[i].sinr);
	else
		SLSI_INFO(sdev, "no cell info\n");

	if (mhs_vif)
		slsi_mlme_set_cellular_info(sdev);

	SLSI_MUTEX_UNLOCK(sdev->cp_coex_mutex);
	unlock_mhs_vif(mhs_vif);

	return 0;
}

void wlbt_register_ril_notifier(struct slsi_dev *sdev)
{
	SLSI_INFO(sdev, "register ril notifier");

	SLSI_MUTEX_INIT(sdev->cp_coex_mutex);
	memset(&sdev->cp_cell_infos, 0, sizeof(struct cp_noti_cell_infos));
	sdev->ril_notifier.notifier_call = wlbt_ril_notifier;
	register_dev_ril_bridge_event_notifier(&sdev->ril_notifier);
}

void wlbt_unregister_ril_notifier(struct slsi_dev *sdev)
{
	SLSI_INFO(sdev, "un-register ril notifier");

	unregister_dev_ril_bridge_event_notifier(&sdev->ril_notifier);
}
