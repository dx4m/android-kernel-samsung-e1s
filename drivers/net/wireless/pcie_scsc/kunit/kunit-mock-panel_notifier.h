/* SPDX-License-Identifier: GPL-2.0+ */

#include "../panel_notifier.h"

#define wlbt_register_panel_state_notifier(args...)    			kunit_mock_wlbt_register_panel_state_notifier(args)
#define wlbt_unregister_panel_state_notifier(args...)			kunit_mock_wlbt_unregister_panel_state_notifier(args)

static void kunit_mock_wlbt_register_panel_state_notifier(struct slsi_dev *sdev)
{
	return;
}

static void kunit_mock_wlbt_unregister_panel_state_notifier(struct slsi_dev *sdev)
{
	return;
}