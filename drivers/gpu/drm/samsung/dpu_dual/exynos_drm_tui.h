/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Headef file for exynos_drm_tui.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __EXYNOS_DRM_TUI_H__
#define __EXYNOS_DRM_TUI_H__

#include <linux/types.h>
#include <drm/drm_crtc.h>

struct stui_buf_info;
struct decon_device;
struct exynos_drm_crtc_state;

#define DISP_ID_MAIN                    (0x0U << 0)
#define DISP_ID_SUB                     (0x1U << 0)
#define DISP_ID_MASK                    (0xFU << 0)
#define DISP_TYPE_SINGLE                (0x0U << 4)
#define DISP_TYPE_DUAL                  (0x1U << 4)
#define DISP_TYPE_MASK                  (0xFU << 4)
#define DISP_MODE_VIDEO                 (0x0U << 8)
#define DISP_MODE_COMMAND               (0x1U << 8)
#define DISP_MODE_MASK                  (0xFU << 8)
#define DISP_FLAG_MASK                  (DISP_ID_MASK | DISP_TYPE_MASK | DISP_MODE_MASK)

struct resolution_info {
	uint32_t xres;
	uint32_t yres;
	uint32_t mode;		/* TODO: Merge to flag */
	uint32_t disp_flag;
};

/* TODO: change with self_refresh */
int exynos_drm_atomic_check_tui(struct drm_atomic_state *state);

int exynos_atomic_enter_tui(void);
int exynos_atomic_exit_tui(void);
int exynos_tui_register(struct drm_device *drmdev);

void exynos_tui_get_resolution(struct resolution_info *res_info);
#if IS_ENABLED(CONFIG_SAMSUNG_TUI)
void exynos_tui_set_stui_funcs(struct stui_buf_info *(*func1)(void), void (*func2)(void));
#endif
void exynos_tui_sec_win_shadow_update_req(struct decon_device *decon,
		struct exynos_drm_crtc_state *old_exynos_crtc_state,
		struct exynos_drm_crtc_state *new_exynos_crtc_state);
void exynos_tui_release_sec_buf(struct decon_device *decon,
		struct exynos_drm_crtc_state *old_exynos_crtc_state,
		struct exynos_drm_crtc_state *new_exynos_crtc_state);
int exynos_tui_get_panel_info(u64 *buf, int size);

#endif /* __EXYNOS_DRM_TUI_H__ */
