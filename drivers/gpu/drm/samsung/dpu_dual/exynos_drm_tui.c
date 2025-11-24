/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * TUI file for Samsung EXYNOS DPU driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_vblank.h>

#include <exynos_drm_crtc.h>
#include <exynos_drm_tui.h>
#include <exynos_drm_hibernation.h>
#include <exynos_drm_plane.h>
#include <exynos_drm_partial.h>

#include <cal_common/decon_cal.h>
#include <cal_common/dpp_cal.h>
#include <exynos_drm_dsim.h>
#include <exynos_drm_decon.h>
#include <exynos_drm_crtc.h>

#if IS_ENABLED(CONFIG_SAMSUNG_TUI)
#include <soc/samsung/exynos-smc.h>
#include "../../../../misc/tui/stui_hal.h"
#include "../../../../misc/tui/stui_core.h"

struct stui_buf_info *(*tui_get_buf_info)(void);
void (*tui_free_video_space)(void);
#endif

static struct dpu_tui {
	struct drm_device *drmdev;
	struct drm_crtc *crtc;
} *tui;

static int tui_log_level = 6;
module_param(tui_log_level, int, 0600);
MODULE_PARM_DESC(tui_log_level, "log level for dpu decon [default : 6]");

#define TUI_NAME "dpu-tui"
#define tui_info(fmt, ...)								\
	dpu_pr_info(TUI_NAME, (tui && tui->crtc) ? drm_crtc_index((tui->crtc)) : -1,	\
			tui_log_level, fmt, ##__VA_ARGS__)
#define tui_warn(fmt, ...)								\
	dpu_pr_warn(TUI_NAME, (tui && tui->crtc) ? drm_crtc_index((tui->crtc)) : -1,	\
			tui_log_level, fmt, ##__VA_ARGS__)
#define tui_err(fmt, ...)								\
	dpu_pr_err(TUI_NAME, (tui && tui->crtc) ? drm_crtc_index((tui->crtc)) : -1,	\
			tui_log_level, fmt, ##__VA_ARGS__)
#define tui_debug(fmt, ...)								\
	dpu_pr_debug(TUI_NAME, (tui && tui->crtc) ? drm_crtc_index((tui->crtc)) : -1,	\
			tui_log_level, fmt, ##__VA_ARGS__)

/* See also: drm_atomic_helper_duplicate_state() */
static struct drm_atomic_state *
__duplicate_atomic_state(struct drm_device *drmdev, struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int err = 0;

	state = drm_atomic_state_alloc(drmdev);
	if (!state)
		return ERR_PTR(-ENOMEM);

	state->acquire_ctx = ctx;
	state->duplicated = true;

	drm_for_each_crtc(crtc, drmdev) {
		if (!crtc || !crtc->state->active || !crtc->state->enable)
			continue;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			err = PTR_ERR(crtc_state);
			goto free;
		}

		err = drm_atomic_add_affected_planes(state, crtc);
		if (err < 0)
			goto free;

		err = drm_atomic_add_affected_connectors(state, crtc);
		if (err < 0)
			goto free;
	}

	/* clear the acquire context so that it isn't accidentally reused */
	state->acquire_ctx = NULL;
free:
	if (err < 0) {
		drm_atomic_state_put(state);
		state = ERR_PTR(err);
	}

	return state;
}

static struct drm_crtc *__get_tui_crtc(struct drm_device *drmdev)
{
	struct drm_crtc *crtc, *active_crtc = NULL;
	struct exynos_drm_crtc *exynos_crtc;
	struct exynos_drm_crtc_state *exynos_crtc_state;
	int active_crtc_cnt;

	active_crtc_cnt = 0;
	drm_for_each_crtc(crtc, drmdev) {
		exynos_crtc = to_exynos_crtc(crtc);

		if (!(exynos_crtc->possible_type & EXYNOS_DISPLAY_TYPE_DSI))
			continue;

		if (!drm_atomic_crtc_effectively_active(crtc->state))
			continue;

		active_crtc = crtc;
		++active_crtc_cnt;
	}

	/* TUI can only be enabled when single display */
	if (active_crtc_cnt == 1) {
		tui_info("tui crtc[%s]\n", active_crtc->name);

		return active_crtc;
	} else if (active_crtc_cnt == 0 && tui->crtc) {
		/* If cnt is 0 and already tui status return the old crtc */
		exynos_crtc_state = to_exynos_crtc_state(tui->crtc->state);
		if (exynos_crtc_state->tui_status) {
			tui_info("tui crtc%s]\n", tui->crtc->name);

			return tui->crtc;
		}
	}

	tui_err("can't enter TUI, active_crtc_cnt(%d) from [%ps]\n",
			active_crtc_cnt, __builtin_return_address(0));
	return NULL;
}

static int __set_new_state(struct drm_atomic_state *new_state)
{
	struct decon_device *decon;
	struct drm_crtc *crtc;
	struct exynos_drm_crtc *exynos_crtc;
	struct drm_plane_state *new_plane_state;
	struct drm_plane *plane;
	struct exynos_drm_plane_state *new_exynos_plane_state;
	struct drm_crtc_state *crtc_state;
	struct exynos_drm_crtc_state *old_exynos_crtc_state, *new_exynos_crtc_state;
	int i, j, ret = 0;
	unsigned long op_mode = 0;

	drm_for_each_crtc(crtc, new_state->dev) {
		i = drm_crtc_index(crtc);
		exynos_crtc = to_exynos_crtc(crtc);
		crtc_state = drm_atomic_get_crtc_state(new_state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		new_exynos_crtc_state = to_exynos_crtc_state(crtc_state);
		new_exynos_crtc_state->tui_status = true;

		if (!drm_atomic_crtc_effectively_active(crtc_state))
			continue;

		old_exynos_crtc_state = to_exynos_crtc_state(crtc->state);
		decon = exynos_crtc->ctx;
		op_mode = decon->config.mode.op_mode;
		tui_info(": Crtc-%d operation mode(%lu)\n", i, op_mode);

		if (op_mode == DECON_COMMAND_MODE) {
			crtc_state->active = false;
			tui_info(": Set crtc_state->active to false\n");
		}

		new_exynos_crtc_state->dqe_fd = -1;
		/* TODO: Modify the set full partial within commit code */
		if (exynos_crtc->partial) {
			drm_wait_one_vblank(crtc->dev, crtc->index);

			exynos_partial_set_full(&new_exynos_crtc_state->base.mode,
					&new_exynos_crtc_state->partial_region);
			tui_info(": x1(%d) y1(%d) x2(%d) y2(%d)\n",
					new_exynos_crtc_state->partial_region.x1,
					new_exynos_crtc_state->partial_region.y1,
					new_exynos_crtc_state->partial_region.x2,
					new_exynos_crtc_state->partial_region.y2);
			exynos_partial_update(exynos_crtc->partial, &old_exynos_crtc_state->partial_region,
					&new_exynos_crtc_state->partial_region);
		}

		/* if need, set the default display mode for TUI */
		for_each_new_plane_in_state(new_state, plane, new_plane_state, j) {
			new_exynos_plane_state = to_exynos_plane_state(new_plane_state);
			new_exynos_plane_state->hdr_fd = -1;
			if (op_mode == DECON_COMMAND_MODE) {
				ret = drm_atomic_set_crtc_for_plane(new_plane_state, NULL);
				if (ret < 0)
					return ret;

				drm_atomic_set_fb_for_plane(new_plane_state, NULL);
			}
		}

		/* shoule set the default disp clock for tui */
		if (IS_ENABLED(CONFIG_EXYNOS_BTS) && (op_mode == DECON_COMMAND_MODE)) {
			if (exynos_pm_qos_request_active(&exynos_crtc->bts->int_qos))
				exynos_pm_qos_update_request(&exynos_crtc->bts->int_qos, 133 * 1000);
			else
				tui_err("int qos setting error\n");

			if (exynos_pm_qos_request_active(&exynos_crtc->bts->disp_qos)) {
				unsigned long disp_minlock_freq = 333 * 1000;
				int index;

				for (index = (exynos_crtc->bts->dfs_lv_cnt - 1); index >= 0; index--) {
					if (exynos_crtc->bts->resol_clk <= exynos_crtc->bts->dfs_lv[index]) {
						disp_minlock_freq = exynos_crtc->bts->dfs_lv[index];
						break;
					}
				}

				exynos_pm_qos_update_request(&exynos_crtc->bts->disp_qos, disp_minlock_freq);
			} else {
				tui_err("disp qos setting error\n");
			}
		}

		DPU_EVENT_LOG("TUI_ENTER", exynos_crtc, 0, "resolution[%ux%u] mode[%s]",
				crtc_state->adjusted_mode.hdisplay,
				crtc_state->adjusted_mode.vdisplay,
				(op_mode == DECON_COMMAND_MODE) ? "Command" : "Video");
	}

	return 0;
}

static void __release_suspend_state(struct drm_mode_config *mode_config)
{
	if (!mode_config->suspend_state)
		return;

	drm_atomic_state_put(mode_config->suspend_state);
	mode_config->suspend_state = NULL;
}

static void all_hibernation_block_exit(void)
{
	struct exynos_drm_crtc *exynos_crtc;
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, tui->drmdev) {
			exynos_crtc = to_exynos_crtc(crtc);
			hibernation_block_exit(exynos_crtc->hibernation);
	}
}

static void all_hibernation_unblock(void)
{
	struct exynos_drm_crtc *exynos_crtc;
	struct drm_crtc *crtc;

		drm_for_each_crtc(crtc, tui->drmdev) {
				exynos_crtc = to_exynos_crtc(crtc);
				hibernation_unblock(exynos_crtc->hibernation);
		}
}

int exynos_drm_atomic_check_tui(struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct exynos_drm_crtc_state *old_exynos_crtc_state, *new_exynos_crtc_state;
	int i;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		new_exynos_crtc_state = to_exynos_crtc_state(new_crtc_state);
		old_exynos_crtc_state = to_exynos_crtc_state(old_crtc_state);

		if (old_exynos_crtc_state->tui_status != new_exynos_crtc_state->tui_status)
			new_exynos_crtc_state->tui_changed = true;

		if (new_exynos_crtc_state->tui_status &&
				!new_exynos_crtc_state->tui_changed) {
			tui_debug("reject commit(%p) : display update in TUI status\n", state);
			return -EPERM;
		}

		/* TODO: delete, HWC will skip clearDisplay for commit fail in TUI state */
		if ((old_exynos_crtc_state->tui_changed && !new_exynos_crtc_state->tui_changed) &&
				new_crtc_state->plane_mask == 0 &&
				old_crtc_state->active) {
			tui_debug("reject commit(%p) : clear display right after TUI exit\n", state);
			return -EPERM;
		}
	}

	return 0;
}

/**
 * exynos_atomic_enter_tui - save the current state and disable lcd display
 *
 * Disable display pipeline for lcd crtc, but skip control for the panel,
 * block power and te irq. The clock(disp, int) should be guaranteed for TUI.
 * Duplicate the current atomic state and this state should be restored in
 * exynos_atomic_exit_tui().
 * See also:
 * drm_atomic_helper_suspend()
 */
int exynos_atomic_enter_tui(void)
{
	struct drm_atomic_state *suspend_state, *new_state;
	struct drm_modeset_acquire_ctx ctx;
	int ret = 0;

	if (!tui->drmdev) {
		tui_err("drmdev for TUI isn't registered");
		return -ENODEV;
	}

	all_hibernation_block_exit();

	DRM_MODESET_LOCK_ALL_BEGIN(tui->drmdev, ctx, 0, ret);

	tui->crtc = __get_tui_crtc(tui->drmdev);
	if (!tui->crtc) {
		ret = -ENODEV;
		goto err_status;
	}

	tui_info("+\n");

	suspend_state = __duplicate_atomic_state(tui->drmdev, &ctx);
	if (IS_ERR(suspend_state)) {
		ret = PTR_ERR(suspend_state);
		goto err_status;
	}

	tui->drmdev->mode_config.suspend_state = suspend_state;

	new_state = __duplicate_atomic_state(tui->drmdev, &ctx);
	if (IS_ERR(new_state)) {
		ret = PTR_ERR(new_state);
		goto err_dup_new_state;
	}
	new_state->acquire_ctx = &ctx;

	ret = __set_new_state(new_state);
	if (ret < 0)
		goto err;

	ret = drm_atomic_commit(new_state);
	if (ret < 0)
		goto err;

err:
	drm_atomic_state_put(new_state);
err_dup_new_state:
	if (ret)
		__release_suspend_state(&tui->drmdev->mode_config);
err_status:
	DRM_MODESET_LOCK_ALL_END(tui->drmdev, ctx, ret);
	all_hibernation_unblock();
	tui_info("ret(%d) -\n", ret);

	return ret;
}
EXPORT_SYMBOL(exynos_atomic_enter_tui);

int exynos_atomic_exit_tui(void)
{
	struct decon_device *decon;
	struct drm_atomic_state *suspend_state;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_plane_state *new_plane_state;
	struct drm_plane *plane;
	struct exynos_drm_plane_state *new_exynos_plane_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	struct exynos_drm_crtc *exynos_crtc;
	struct exynos_drm_crtc_state *new_exynos_crtc_state;
	int ret, i, j;
	unsigned long op_mode = 0;
	bool is_video[MAX_DECON_CNT] = { false, };

	if (!tui->drmdev) {
		tui_err("drmdev for TUI isn't registered");
		return -ENODEV;
	}

	DRM_MODESET_LOCK_ALL_BEGIN(tui->drmdev, ctx, 0, ret);

	if (!tui->crtc) {
		ret = -ENODEV;
		goto out;
	}

	tui_info("+\n");

	/* if necessary, fix up suspend atomic suspend_state. */
	suspend_state = tui->drmdev->mode_config.suspend_state;
	if (!suspend_state) {
		tui_err("there is not suspend_state\n");
		ret = -EINVAL;
		goto out;
	}

	suspend_state->acquire_ctx = &ctx;

	drm_for_each_crtc(crtc, tui->drmdev) {
		i = drm_crtc_index(crtc);
		exynos_crtc = to_exynos_crtc(crtc);
		new_crtc_state = drm_atomic_get_crtc_state(suspend_state, crtc);
		if (IS_ERR(new_crtc_state)) {
			ret = PTR_ERR(new_crtc_state);
			goto err;
		}
		new_exynos_crtc_state = to_exynos_crtc_state(new_crtc_state);
		new_exynos_crtc_state->tui_status = false;

		if (!drm_atomic_crtc_effectively_active(crtc->state))
			continue;

		decon = exynos_crtc->ctx;
		op_mode = decon->config.mode.op_mode;
		is_video[i] = (op_mode == DECON_VIDEO_MODE);
		tui_info(": Crtc-%d operation mode(%lu)\n", i, op_mode);

		new_exynos_crtc_state->dqe_fd = -1;
		if (!is_video[i]) {
			new_exynos_crtc_state->reserved_win_mask = 0;
			new_exynos_crtc_state->freed_win_mask = 0;
			new_exynos_crtc_state->visible_win_mask = 0;
		}

		for_each_new_plane_in_state(suspend_state, plane, new_plane_state, j) {
			new_exynos_plane_state = to_exynos_plane_state(new_plane_state);
			new_exynos_plane_state->hdr_fd = -1;
		}

		if (exynos_crtc->partial) {
			exynos_partial_set_full(&new_exynos_crtc_state->base.mode,
					&new_exynos_crtc_state->partial_region);
		}
	}

	ret = drm_atomic_helper_commit_duplicated_state(suspend_state, &ctx);
	if (ret < 0) {
		tui_err("failed to atomic commit suspend_state(0x%x)\n", ret);
		goto err;
	}

	DPU_EVENT_LOG("TUI_EXIT", exynos_crtc, 0, NULL);
err:
	__release_suspend_state(&tui->drmdev->mode_config);
out:
	tui_info("ret(%d) -\n", ret);
	tui->crtc = NULL;
	DRM_MODESET_LOCK_ALL_END(tui->drmdev, ctx, ret);

	return ret;
}
EXPORT_SYMBOL(exynos_atomic_exit_tui);

int exynos_tui_register(struct drm_device *drmdev)
{
	tui = drmm_kzalloc(drmdev, sizeof(*tui), GFP_KERNEL);
	if (!tui)
		return -ENOMEM;

	tui->drmdev = drmdev;

	return 0;
}

void exynos_tui_get_resolution(struct resolution_info *res_info)
{
	struct drm_modeset_acquire_ctx ctx;
	struct exynos_drm_crtc_state *exynos_crtc_state;
	struct decon_device *decon;
	unsigned long op_mode = 0;
	int ret = 0;

	if (!tui->drmdev)
		return;

	memset(res_info, 0, sizeof(res_info));

	DRM_MODESET_LOCK_ALL_BEGIN(tui->drmdev, ctx, 0, ret);

	tui->crtc = __get_tui_crtc(tui->drmdev);
	if (!tui->crtc)
		goto out;

	tui_info("+\n");

	res_info->xres = tui->crtc->state->adjusted_mode.hdisplay;
	res_info->yres = tui->crtc->state->adjusted_mode.vdisplay;
	exynos_crtc_state = to_exynos_crtc_state(tui->crtc->state);

	decon = to_exynos_crtc(tui->crtc)->ctx;
	op_mode = decon->config.mode.op_mode;
	if (op_mode == DECON_VIDEO_MODE) {
		res_info->mode = 1; /* video mode */
		res_info->disp_flag |= DISP_MODE_VIDEO;
	} else {
		res_info->mode = 0; /* command mode */
		res_info->disp_flag |= DISP_MODE_COMMAND;
	}

	res_info->disp_flag |= drm_crtc_index(tui->crtc) & DISP_ID_MASK;

	res_info->disp_flag |= DISP_TYPE_SINGLE;
	if (decon->config.mode.dsi_mode == DSI_MODE_DUAL_DISPLAY)
		res_info->disp_flag |= DISP_TYPE_DUAL;

out:
	DRM_MODESET_LOCK_ALL_END(tui->drmdev, ctx, ret);

	tui_info("width(%d) height(%d) mode(%#x) disp_flag(%#x) -\n",
			res_info->xres, res_info->yres, res_info->mode, res_info->disp_flag);
}
EXPORT_SYMBOL(exynos_tui_get_resolution);

#if IS_ENABLED(CONFIG_SAMSUNG_TUI)
void exynos_tui_set_stui_funcs(struct stui_buf_info *(*func1)(void), void (*func2)(void))
{
	tui_get_buf_info = func1;
	tui_free_video_space = func2;
}
EXPORT_SYMBOL(exynos_tui_set_stui_funcs);

#define SMC_DRM_TUI_UNPROT		(0x82002121)
#define SMC_DPU_SEC_SHADOW_UPDATE_REQ	(0x82002122)

#define DEV_COMMAND_MODE	0
#define DEV_VIDEO_MODE		1

void exynos_tui_sec_win_shadow_update_req(struct decon_device *decon,
		struct exynos_drm_crtc_state *old_exynos_crtc_state,
		struct exynos_drm_crtc_state *new_exynos_crtc_state)
{
	int ret;
	struct decon_config *cfg;

	if ((old_exynos_crtc_state->tui_changed == 1)
			&& (new_exynos_crtc_state->tui_changed == 0)) {
		if (decon->id != 0)
			return;

		cfg = &decon->config;

		if (cfg->mode.op_mode == DECON_VIDEO_MODE) {
			tui_info("SMC_DPU_SEC_SHADOW_UPDATE_REQ called\n");
			ret = exynos_smc(SMC_DPU_SEC_SHADOW_UPDATE_REQ, 0, 0, 0);
			if (ret)
				tui_err("shadow_update_req smc_call error\n");
		}
	}
}

void exynos_tui_release_sec_buf(struct decon_device *decon,
		struct exynos_drm_crtc_state *old_exynos_crtc_state,
		struct exynos_drm_crtc_state *new_exynos_crtc_state)
{
	int ret;
	struct decon_config *cfg;
	struct stui_buf_info *tui_buf_info;

	if ((old_exynos_crtc_state->tui_changed == 1)
			&& (new_exynos_crtc_state->tui_changed == 0)) {
		if (decon->id != 0)
			return;

		cfg = &decon->config;

		if (cfg->mode.op_mode == DECON_VIDEO_MODE) {
			tui_buf_info = tui_get_buf_info();

			ret = exynos_smc(SMC_DRM_TUI_UNPROT, tui_buf_info->pa[0],
					tui_buf_info->size[0] + tui_buf_info->size[1], DEV_VIDEO_MODE);
			if (ret)
				tui_err("release_buf smc_call error\n");

			tui_free_video_space();
		}
	}
}
#else
void exynos_tui_sec_win_shadow_update_req(struct decon_device *decon,
		struct exynos_drm_crtc_state *old_exynos_crtc_state,
		struct exynos_drm_crtc_state *new_exynos_crtc_state)
{
}
void exynos_tui_release_sec_buf(struct decon_device *decon,
		struct exynos_drm_crtc_state *old_exynos_crtc_state,
		struct exynos_drm_crtc_state *new_exynos_crtc_state)
{
}
#endif

#define MIN_BUF_SIZE 10
int exynos_tui_get_panel_info(u64 *buf, int size)
{
	struct dsim_device *dsim_dev;
	struct dsim_reg_config *dsim_config;
	struct dpu_panel_timing *timing;
	struct exynos_dsc *dsc;
	struct dsim_clks *clks;
	struct stdphy_pms *pms;
	int dsim_id;
	int idx = 0;
	u64 data, max;

	if (!tui->drmdev)
		return -EINVAL;

	tui->crtc = __get_tui_crtc(tui->drmdev);
	if (!tui->crtc)
		return -EINVAL;

	dsim_id = (drm_crtc_index(tui->crtc) == 0) ? 0 : 1;
	dsim_dev = get_dsim_drvdata(dsim_id);
	dsim_config = &dsim_dev->config;
	timing = &dsim_config->p_timing;
	dsc = &dsim_config->dsc;
	clks = &dsim_dev->clk_param;
	pms = &dsim_config->dphy_pms;

	if (size < MIN_BUF_SIZE) {
		tui_err("Buffer size is too small!\n");
		return -EINVAL;
	}

	data = ((u64)timing->vactive * 100000000) +
		((u64)timing->hactive * 10000) + ((u64)timing->vrefresh);
	buf[idx++] = data;

	max = (timing->vfp > timing->vsa) ? timing->vfp : timing->vsa;
	max = (timing->vbp > max) ? timing->vbp : max;
	max = (timing->hfp > max) ? timing->hfp : max;
	max = (timing->hsa > max) ? timing->hsa : max;
	max = (timing->hbp > max) ? timing->hbp : max;
	if (max >= 100) {
		data = ((u64)timing->vfp * 10000000000)
			+ ((u64)timing->vsa * 100000000)
			+ ((u64)timing->vbp * 1000000)
			+ ((u64)timing->hfp * 10000)
			+ ((u64)timing->hsa * 100)
			+ ((u64)timing->hbp);
		buf[idx++] = data;
	} else {
		data = ((u64)timing->vfp * 10000000000)
			+ ((u64)timing->vsa * 100000)
			+ ((u64)timing->vbp);
		data |= (1ULL << 63);
		buf[idx++] = data;
		data = ((u64)timing->hfp * 10000000000)
			+ ((u64)timing->hsa * 100000)
			+ ((u64)timing->hbp);
		buf[idx++] = data;
	}

	data = ((u64)dsc->slice_height * 1000) + ((u64)dsc->slice_count * 100)
		+ ((u64)dsc->dsc_count * 10) + ((u64)dsim_config->data_lane_cnt);
	data |= ((dsc->enabled) ? (1ULL << 63) : 0);
	data |= ((dsim_config->mode == DSIM_COMMAND_MODE) ? (1ULL << 62) : 0);
	data |= ((pms->dither_en) ? (1ULL << 61) : 0);
	buf[idx++] = data;

	data = ((u64)clks->hs_clk * 10000000000)
		+ ((u64)clks->esc_clk * 100000)
		+ ((u64)dsim_config->cmd_underrun_cnt);
	buf[idx++] = data;

	max = (pms->mfr) | (pms->mrr) | (pms->sel_pf) | (pms->icp)
		| (pms->afc_enb) | (pms->extafc) | (pms->feed_en)
		| (pms->fsel) | (pms->fout_mask) | (pms->rsel);

	if (max == 0) {
		data = ((u64)pms->p * 10000000000) + ((u64)pms->m * 100000) + ((u64)pms->s);
		buf[idx++] = data;
		data = ((u64)pms->k * 10000000000);
		buf[idx++] = data;
	} else {
		data = ((u64)pms->p * 10000000000) + ((u64)pms->m * 100000) + ((u64)pms->s);
		data |= (1ULL << 63);
		buf[idx++] = data;
		data = ((u64)pms->k * 10000000000) + ((u64)pms->mfr * 100000) + ((u64)pms->mrr);
		buf[idx++] = data;
		data = ((u64)pms->sel_pf * 10000000000) + ((u64)pms->icp * 100000) + ((u64)pms->afc_enb);
		buf[idx++] = data;
		data = ((u64)pms->extafc * 10000000000) + ((u64)pms->feed_en * 100000) + ((u64)pms->fsel);
		buf[idx++] = data;
		data = ((u64)pms->fout_mask * 100000) + ((u64)pms->rsel);
		buf[idx++] = data;
	}

	return 0;
}
EXPORT_SYMBOL(exynos_tui_get_panel_info);
