/******************************************************************************
 *
 * Copyright (c) 2012 - 2024 Samsung Electronics Co., Ltd. All rights reserved
 *
 *****************************************************************************/

#include "dev.h"
#include "mlme.h"
#include "mgt.h"

u16 slsi_get_vifnum_by_ifnum(struct slsi_dev *sdev, int ifnum)
{
	u16 i = 0;

	for (i = FAPI_VIFRANGE_VIF_INDEX_MIN; i < FAPI_VIFRANGE_VIF_INDEX_MAX - (SLSI_NAN_MGMT_VIF_NUM(sdev)); i++)
		if (sdev->vif_netdev_id_map[i] == ifnum)
			return i;
	SLSI_ERR(sdev, "Can't find vifnum for ifnum : %d\n", ifnum);

	return SLSI_INVALID_VIF;
}

int slsi_get_ifnum_by_vifid(struct slsi_dev *sdev, u16 vif_id)
{
	if (vif_id <= SLSI_NAN_MGMT_VIF_NUM(sdev) && vif_id >= FAPI_VIFRANGE_VIF_INDEX_MIN)
		return sdev->vif_netdev_id_map[vif_id];
	if (vif_id == 0)
		return 0;
	if (vif_id <= FAPI_VIFRANGE_VIF_INDEX_MAX)
		return SLSI_NAN_DATA_IFINDEX_START;

	return SLSI_INVALID_IFNUM;
}

void slsi_mlme_assign_vif(struct slsi_dev *sdev, struct net_device *dev, u16 vif_id)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	sdev->vif_netdev_id_map[vif_id] = ndev_vif->ifnum;
	ndev_vif->vifnum = vif_id;
	SLSI_INFO(sdev, "Assigned vif: %d to ifnum: %d\n", ndev_vif->vifnum, ndev_vif->ifnum);
}

void slsi_mlme_clear_vif(struct slsi_dev *sdev, struct net_device *dev, u16 vif_id)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	sdev->vif_netdev_id_map[vif_id] = SLSI_INVALID_IFNUM;
	ndev_vif->vifnum = SLSI_INVALID_VIF;
	SLSI_INFO(sdev, "Cleared vif for ifnum: %d\n", ndev_vif->ifnum);
}

bool slsi_is_valid_vifnum(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	if (!dev) {
		SLSI_ERR(sdev, "Invalid Dev");
		return false;
	}
	if (ndev_vif->vifnum < FAPI_VIFRANGE_VIF_INDEX_MIN || ndev_vif->vifnum > FAPI_VIFRANGE_VIF_INDEX_MAX) {
		SLSI_NET_ERR(dev, "Invalid Vifnum %d for ifnum %d\n" , ndev_vif->vifnum ,ndev_vif->ifnum);
		return false;
	}
	return true;
}

void slsi_mlme_assign_detect_vif(struct slsi_dev *sdev, struct net_device *dev, u16 vif_id)
{
       struct netdev_vif *ndev_vif = netdev_priv(dev);

       ndev_vif->detect_vifnum = vif_id;
       sdev->vif_netdev_id_map[vif_id] = ndev_vif->ifnum;
       SLSI_INFO(sdev, "Assigned Detect vif: %d to ifnum: %d\n", ndev_vif->detect_vifnum, ndev_vif->ifnum);
}

void slsi_mlme_clear_detect_vif(struct slsi_dev *sdev, struct net_device *dev, u16 vif_id)
{
       struct netdev_vif *ndev_vif = netdev_priv(dev);

       sdev->vif_netdev_id_map[vif_id] = SLSI_INVALID_IFNUM;
       ndev_vif->detect_vifnum = 0;
       SLSI_INFO(sdev, "Cleared Detect vif for ifnum: %d\n", ndev_vif->ifnum);
}

void slsi_monitor_set_if_with_vif(struct slsi_dev *sdev, struct netdev_vif *ndev_vif, int ifnum)
{
	u16 vifnum = slsi_get_vifnum_by_ifnum(sdev, ifnum);

	ndev_vif->ifnum = ifnum;
	if (vifnum == SLSI_INVALID_VIF)
		ndev_vif->vifnum = ifnum;
	else
		ndev_vif->vifnum = vifnum;
}
