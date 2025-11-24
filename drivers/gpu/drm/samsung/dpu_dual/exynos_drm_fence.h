/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2025 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_DRM_FENCE_H_
#define _EXYNOS_DRM_FENCE_H_

struct exynos_out_fence {
        s32 __user *ptr;
        struct sync_file *sync_file;
        int fd;
};

struct exynos_drm_crtc_state;
int exynos_out_fence_setup(struct drm_crtc_state *crtc_state);
void exynos_out_fence_install_fds(struct drm_atomic_state *state, bool install_fds);
void exynos_out_fence_clear(struct drm_crtc_state *crtc_state);

#endif
