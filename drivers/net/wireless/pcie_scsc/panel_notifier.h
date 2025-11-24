/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************
 *
 * Copyright (c) 2012 - 2025 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef __SLSI_PANEL_STATE_NOTIFIER_H__
#define __SLSI_PANEL_STATE_NOTIFIER_H__

#include "dev.h"

int wlbt_panel_state_notifier(struct notifier_block *nb, unsigned long event, void *data);
void wlbt_register_panel_state_notifier(struct slsi_dev *sdev);
void wlbt_unregister_panel_state_notifier(struct slsi_dev *sdev);

#if !defined(SCSC_SEP_VERSION)
enum panel_notifier_event_state_t {
        PANEL_EVENT_STATE_NONE,

        /* PANEL_EVENT_BL_STATE_CHANGED */

        /* PANEL_EVENT_VRR_STATE_CHANGED */

        /* PANEL_EVENT_LFD_STATE_CHANGED */

        /* PANEL_EVENT_PANEL_STATE_CHANGED */
        PANEL_EVENT_PANEL_STATE_OFF,
        PANEL_EVENT_PANEL_STATE_ON,
        PANEL_EVENT_PANEL_STATE_LPM,

        /* PANEL_EVENT_UB_CON_STATE_CHANGED */
        PANEL_EVENT_UB_CON_STATE_CONNECTED,
        PANEL_EVENT_UB_CON_STATE_DISCONNECTED,

        /* PANEL_EVENT_COPR_STATE_CHANGED */
        PANEL_EVENT_COPR_STATE_DISABLED,
        PANEL_EVENT_COPR_STATE_ENABLED,

        /* PANEL_EVENT_TEST_MODE_STATE_CHANGED */
        PANEL_EVENT_TEST_MODE_STATE_NONE,
        PANEL_EVENT_TEST_MODE_STATE_GCT,

        /* PANEL_EVENT_SCREEN_MODE_STATE_CHANGED */

        /* PANEL_EVENT_ESD_STATE_CHANGED */

        MAX_PANEL_EVENT_STATE,
};

struct panel_notifier_event_data {
        /* base */
        unsigned int display_index;
        /* state */
        enum panel_notifier_event_state_t state;
};
#endif

struct panel_work {
	struct work_struct work;
	struct slsi_dev *sdev;
	int state;
};

#endif /* __SLSI_PANEL_STATE_NOTIFIER_H__ */
