/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2025 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/dma-fence.h>
#include <linux/sync_file.h>

#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_vblank.h>

#include <exynos_drm_fence.h>
#include <exynos_drm_crtc.h>
#include <exynos_drm_fb.h>

static const struct dma_fence_ops exynos_crtc_fence_ops;

static struct drm_crtc *fence_to_crtc(struct dma_fence *fence)
{
	BUG_ON(fence->ops != &exynos_crtc_fence_ops);
	return container_of(fence->lock, struct drm_crtc, fence_lock);
}

static const char *__crtc_fence_get_driver_name(struct dma_fence *fence)
{
	struct drm_crtc *crtc = fence_to_crtc(fence);

	return crtc->dev->driver->name;
}

static const char *__crtc_fence_get_timeline_name(struct dma_fence *fence)
{
	struct drm_crtc *crtc = fence_to_crtc(fence);

	return crtc->timeline_name;
}

static const struct dma_fence_ops exynos_crtc_fence_ops = {
	.get_driver_name = __crtc_fence_get_driver_name,
	.get_timeline_name = __crtc_fence_get_timeline_name,
};

static struct dma_fence *__crtc_create_fence(struct drm_crtc *crtc)
{
       struct dma_fence *fence;

       fence = kzalloc(sizeof(*fence), GFP_KERNEL);
       if (!fence)
               return NULL;

       dma_fence_init(fence, &exynos_crtc_fence_ops, &crtc->fence_lock,
                      crtc->fence_context, ++crtc->fence_seqno);

       return fence;
}

int exynos_out_fence_setup(struct drm_crtc_state *crtc_state)
{
	struct exynos_drm_crtc_state *exynos_crtc_state = to_exynos_crtc_state(crtc_state);
	struct exynos_out_fence *out_fence = &exynos_crtc_state->out_fence;
	struct drm_pending_vblank_event *event = crtc_state->event;
	struct dma_fence *fence;
	struct sync_file *sync_file;
	int fd;

	if (event->base.fence || !out_fence->ptr)
		return 0;

	fence =	__crtc_create_fence(crtc_state->crtc);
	if (!fence)
		return -EINVAL;
	event->base.fence = fence;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return -ENOMEM;
	out_fence->fd = fd;

	if (put_user(fd, out_fence->ptr))
		return -EFAULT;

	sync_file = sync_file_create(fence);
	if (!sync_file)
		return -ENOMEM;
	out_fence->sync_file = sync_file;

	return 0;
}

void exynos_out_fence_install_fds(struct drm_atomic_state *state, bool install_fds)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct exynos_drm_crtc_state *new_exynos_crtc_state;
	struct exynos_out_fence *out_fence;
	int i;

	if (!install_fds)
		return;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		new_exynos_crtc_state = to_exynos_crtc_state(new_crtc_state);
		out_fence = &new_exynos_crtc_state->out_fence;
		if (out_fence->fd >= 0 && out_fence->sync_file)
			fd_install(out_fence->fd, out_fence->sync_file->file);
	}
}

void exynos_out_fence_clear(struct drm_crtc_state *crtc_state)
{
	struct exynos_drm_crtc_state *exynos_crtc_state = to_exynos_crtc_state(crtc_state);
	struct exynos_out_fence *out_fence = &exynos_crtc_state->out_fence;
	struct drm_pending_vblank_event *event = crtc_state->event;

	if (!event || !event->base.fence || !out_fence->ptr)
		return;

	dma_fence_put(event->base.fence);
	event->base.fence = NULL;

	if (out_fence->sync_file)
		fput(out_fence->sync_file->file);
	if (out_fence->fd >= 0)
		put_unused_fd(out_fence->fd);
	if (out_fence->ptr && put_user(-1, out_fence->ptr))
		pr_err("Couldn't clear out_fence_ptr\n");
}
