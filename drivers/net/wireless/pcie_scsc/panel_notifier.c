// SPDX-License-Identifier: GPL-2.0
/****************************************************************************
 *
 * Copyright (c) 2012 - 2025 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/


#include <linux/notifier.h>
#include "panel_notifier.h"
#include "debug.h"
#include "mlme.h"
#if defined(SCSC_SEP_VERSION)
#include <linux/sec_panel_notifier_v2.h>
#else
static inline int panel_notifier_register(struct notifier_block *nb) {return 0; }
static inline int panel_notifier_unregister(struct notifier_block *nb) {return 0; }
static inline int panel_notifier_call_chain(unsigned long val, void *v) {return 0; }
#endif

static void wlbt_panel_work(struct work_struct *work)
{
	struct panel_work *panelwork = container_of(work, struct panel_work, work);
	struct slsi_dev *sdev = panelwork->sdev;
	enum slsi_hip_state hip_state;
	u16 host_state;
	int ret = 0;

	mutex_lock(&sdev->hip.hip_mutex);
	hip_state = atomic_read(&sdev->hip.hip_state);
	if (hip_state != SLSI_HIP_STATE_STARTED) {
		mutex_unlock(&sdev->hip.hip_mutex);
		SLSI_ERR(sdev, "hip_state : %d\n", hip_state);
		kfree(panelwork);
		return;
	}

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	host_state = sdev->device_config.host_state;
	if (panelwork->state == PANEL_EVENT_PANEL_STATE_ON) {
		SLSI_INFO(sdev, "Panel Event : PANEL_EVENT_PANEL_STATE_ON\n");
		host_state = host_state | SLSI_HOSTSTATE_LCD_ACTIVE;
	} else if (panelwork->state == PANEL_EVENT_PANEL_STATE_OFF) {
		SLSI_INFO(sdev, "Panel Event : PANEL_EVENT_PANEL_STATE_OFF\n");
		host_state = host_state & ~SLSI_HOSTSTATE_LCD_ACTIVE;
	}
	sdev->device_config.host_state = host_state;
	ret = slsi_mlme_set_host_state(sdev, NULL, host_state);
	if (ret != 0)
		SLSI_ERR(sdev, "Error in setting the Host State, ret=%d\n", ret);

	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	mutex_unlock(&sdev->hip.hip_mutex);

	kfree(panelwork);
}

int wlbt_panel_state_notifier(struct notifier_block *nb, unsigned long event, void *data)
{
	struct slsi_dev *sdev = container_of(nb, struct slsi_dev, panel_notifier);
	struct panel_notifier_event_data *msg = (struct panel_notifier_event_data *)data;
	struct panel_work *panelwork;

	if (!sdev->panel_wq) {
		sdev->panel_wq = alloc_ordered_workqueue("wlbt_panel_wq", 0);
		if (!sdev->panel_wq) {
			SLSI_ERR(sdev, "alloc wq failed\n");
			return -ENOMEM;
		}
	}

	if (!msg) {
		SLSI_ERR(sdev, "Received NULL msg\n");
		return -EINVAL;
	}

	if (msg->state != PANEL_EVENT_PANEL_STATE_ON && msg->state != PANEL_EVENT_PANEL_STATE_OFF)
		return -EOPNOTSUPP;

	panelwork = kzalloc(sizeof(*panelwork), GFP_KERNEL);
	if (!panelwork) {
		SLSI_ERR(sdev, "alloc work failed\n");
		return -ENOMEM;
	}

	panelwork->sdev = sdev;
	panelwork->state = msg->state;

	INIT_WORK(&panelwork->work, wlbt_panel_work);
	queue_work(sdev->panel_wq, &panelwork->work);

	return 0;
}

void wlbt_register_panel_state_notifier(struct slsi_dev *sdev)
{
	int ret = 0;
	SLSI_INFO(sdev, "Register panel notifier\n");
	sdev->panel_notifier.notifier_call = wlbt_panel_state_notifier;
	ret = panel_notifier_register(&sdev->panel_notifier);
	if (ret) {
		SLSI_ERR(sdev, "failed to register panel notifier\n");
		return;
	}

	sdev->panel_wq = alloc_ordered_workqueue("wlbt_panel_wq", 0);
}

void wlbt_unregister_panel_state_notifier(struct slsi_dev *sdev)
{
	SLSI_INFO(sdev, "UN-Register panel notifier\n");
	panel_notifier_unregister(&sdev->panel_notifier);

	if (sdev->panel_wq) {
		flush_workqueue(sdev->panel_wq);
		destroy_workqueue(sdev->panel_wq);
	}
}
