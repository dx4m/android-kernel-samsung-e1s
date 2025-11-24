// SPDX-License-Identifier: GPL-2.0-only
/* exynos_drm_hibernation.c
 *
 * Copyright (C) 2020 Samsung Electronics Co.Ltd
 * Authors:
 *	Jiun Yu <jiun.yu@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/atomic.h>
#include <uapi/linux/sched/types.h>

#include <dpu_trace.h>

#include <drm/drm_modeset_helper_vtables.h>
#include <exynos_drm_hibernation.h>
#include <exynos_drm_crtc.h>
#include <exynos_drm_decon.h>
#include <exynos_drm_writeback.h>
#include <exynos_drm_partial.h>

#include "panel/panel-samsung-drv.h"

#define HIBERNATION_ENTRY_MIN_TIME_NS		(50 * NSEC_PER_MSEC)

static int dpu_hiber_log_level = 6;
module_param(dpu_hiber_log_level, int, 0600);
MODULE_PARM_DESC(dpu_hiber_log_level, "log level for hibernation [default : 6]");

#define HIBER_NAME "exynos-hiber"
#define hiber_info(hiber, fmt, ...)	\
	dpu_pr_info(HIBER_NAME, (hiber)->id, dpu_hiber_log_level, fmt, ##__VA_ARGS__)

#define hiber_warn(hiber, fmt, ...)	\
	dpu_pr_warn(HIBER_NAME, (hiber)->id, dpu_hiber_log_level, fmt, ##__VA_ARGS__)

#define hiber_err(hiber, fmt, ...)	\
	dpu_pr_err(HIBER_NAME, (hiber)->id, dpu_hiber_log_level, fmt, ##__VA_ARGS__)

#define hiber_debug(hiber, fmt, ...)	\
	dpu_pr_debug(HIBER_NAME, (hiber)->id, dpu_hiber_log_level, fmt, ##__VA_ARGS__)

static bool is_dsr_operating(struct exynos_hibernation *hiber)
{
	struct drm_crtc_state *crtc_state = hiber->decon->crtc->base.state;
	struct exynos_drm_crtc_state *exynos_crtc_state;

	if (!crtc_state)
		return false;

	exynos_crtc_state = to_exynos_crtc_state(crtc_state);

	return exynos_crtc_state->dsr_status;
}

static bool is_dim_operating(struct exynos_hibernation *hiber)
{
	const struct decon_device *decon = hiber->decon;

	if (!decon)
		return false;

	return decon->dimming;
}

static bool is_queueing_work(struct exynos_hibernation *hiber)
{
	const struct decon_device *decon = hiber->decon;
	struct exynos_drm_crtc *exynos_crtc;

	if (!decon)
		return false;

	exynos_crtc = decon->crtc;

	return drm_modeset_is_locked(&exynos_crtc->base.mutex);
}

int exynos_hibernation_queue_exit_work(struct exynos_drm_crtc *exynos_crtc)
{
	if (!exynos_crtc->hibernation)
		return -EPERM;

	kthread_queue_work(&exynos_crtc->hibernation->worker,
			&exynos_crtc->hibernation->exit_work);

	return 0;
}

static ssize_t exynos_show_hibernation_exit(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct decon_device *decon = dev_get_drvdata(dev);
	struct exynos_drm_crtc *exynos_crtc = decon->crtc;
	char *p = buf;
	int len = 0;

	hiber_debug(exynos_crtc->hibernation, "+\n");
	hibernation_trig_reset(exynos_crtc->hibernation);

	/* If decon is OFF state, just return here */
	if (decon->state == DECON_STATE_OFF)
		return len;

	if (exynos_hibernation_queue_exit_work(exynos_crtc))
		return len;

	len = sprintf(p, "%d\n", ++exynos_crtc->hibernation->early_wakeup_cnt);
	hiber_debug(exynos_crtc->hibernation, "-\n");
	return len;
}
static DEVICE_ATTR(hiber_exit, S_IRUGO, exynos_show_hibernation_exit, NULL);

void hibernation_trig_reset(struct exynos_hibernation *hiber)
{
	if (!hiber || !hiber->decon->crtc)
		return;

	hiber->entry_time = ktime_add(ktime_get(), HIBERNATION_ENTRY_MIN_TIME_NS);
}

static int dpu_hiber_enable_mask;
module_param(dpu_hiber_enable_mask, int, 0600);
MODULE_PARM_DESC(dpu_hiber_enable_mask, "dpu hiber enable mask [default : false]");
static bool exynos_hibernation_check(struct exynos_hibernation *hiber)
{
	hiber_debug(hiber, "+\n");

	if (!hiber->available || !(dpu_hiber_enable_mask & (1 << hiber->id))) {
		hiber_debug(hiber, "hibernation is intentionally disabled\n");
		return false;
	}

	if (is_hibernaton_blocked(hiber))
		goto reset;

	if (is_dsr_operating(hiber))
		goto reset;

	if (is_dim_operating(hiber))
		goto reset;

	if (is_queueing_work(hiber))
		goto reset;

	return (hiber->entry_time && ktime_after(ktime_get(), hiber->entry_time));

reset:
	hibernation_trig_reset(hiber);

	return false;
}

static bool needs_self_refresh_change(struct drm_crtc_state *old_crtc_state, bool sr_active)
{
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(old_crtc_state->crtc);
	struct exynos_hibernation *hiber = exynos_crtc->hibernation;

	if (sr_active && !ktime_after(ktime_get(), hiber->entry_time))
		return false;

	if (!old_crtc_state->enable) {
		hiber_debug(hiber, "skipping due to crtc disabled\n");
		return false;
	}

	if ((old_crtc_state->self_refresh_active == sr_active) ||
			(old_crtc_state->active == !sr_active)) {
		hiber_debug(hiber, "skipping due to active=%d sr_active=%d, requested sr_active=%d\n",
				old_crtc_state->active, old_crtc_state->self_refresh_active,
				sr_active);
		return false;
	}

	return true;
}

static int exynos_crtc_self_refresh_update(struct drm_crtc *crtc, bool sr_active)
{
	struct drm_device *dev = crtc->dev;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *state;
	struct drm_connector *conn;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state, *old_crtc_state;
	struct exynos_drm_crtc_state *exynos_crtc_state;
	int i, ret = 0;
	ktime_t start = ktime_get();
	struct exynos_hibernation *hiber = to_exynos_crtc(crtc)->hibernation;

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(dev);
	if (!state) {
		ret = -ENOMEM;
		goto out_drop_locks;
	}

retry:
	state->acquire_ctx = &ctx;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto out;
	}

	old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	if (!needs_self_refresh_change(old_crtc_state, sr_active))
		goto out;

	crtc_state->active = !sr_active;
	crtc_state->self_refresh_active = sr_active;
	exynos_crtc_state = to_exynos_crtc_state(crtc_state);
	exynos_crtc_state->skip_update = !sr_active;

	ret = drm_atomic_add_affected_connectors(state, crtc);
	if (ret)
		goto out;

	for_each_new_connector_in_state(state, conn, conn_state, i) {
		if (!conn_state->self_refresh_aware)
			goto out;
	}

	ret = drm_atomic_commit(state);
	if (ret)
		goto out;

out:
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		ret = drm_modeset_backoff(&ctx);
		if (!ret)
			goto retry;
	}

	drm_atomic_state_put(state);

out_drop_locks:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	if (!ret) {
		ktime_t delta = ktime_sub(ktime_get(), start);

		if (ktime_to_us(delta) > 8000)
			hiber_debug(hiber, "%s took %lldus\n", sr_active ? "on" : "off",
					ktime_to_us(delta));
	} else if (ret == -EBUSY) {
		hiber_info(hiber, "aborted due to normal commit pending\n");
	} else {
		hiber_warn(hiber, "unable to enter self refresh (%d)\n", ret);
	}

	return ret;
}

static void __exynos_hibernation_handler(struct exynos_hibernation *hiber, bool entry)
{
        struct decon_device *decon = hiber->decon;
        struct exynos_drm_crtc *exynos_crtc;
        char *event_name;
        int ret;

        event_name = entry ? "HIBER_ENTER" : "HIBER_EXIT";
        hiber_debug(hiber, "%s+\n", event_name);

        if (!decon)
                return;

        exynos_crtc = decon->crtc;

        mutex_lock(&hiber->lock);
        hibernation_block(hiber);

        DPU_ATRACE_BEGIN(event_name);

        DPU_EVENT_LOG(event_name, decon->crtc, 0, "+ DPU POWER %s",
                        pm_runtime_active(exynos_crtc->dev) ? "ON" : "OFF");

        ret = exynos_crtc_self_refresh_update(&exynos_crtc->base, entry);
        if (ret) {
                hiber_err(hiber, "failed to commit self refresh\n");
                goto out;
        }

        if (entry)
                dpu_profile_hiber_enter(exynos_crtc);
        else
                dpu_profile_hiber_exit(exynos_crtc);
out:
        DPU_EVENT_LOG(event_name, decon->crtc, 0, "- DPU POWER %s",
                        pm_runtime_active(exynos_crtc->dev) ? "ON" : "OFF");
        DPU_ATRACE_END(event_name);
        hibernation_unblock(hiber);
        mutex_unlock(&hiber->lock);

        hiber_debug(hiber, "%s- DPU power %s -\n",
                        event_name, pm_runtime_active(decon->dev) ? "on" : "off");
}

void hibernation_block_exit(struct exynos_hibernation *hiber)
{
	if (!hiber)
		return;

	hibernation_block(hiber);
	__exynos_hibernation_handler(hiber, false);
}

static void exynos_hibernation_handler(struct kthread_work *work)
{
	struct exynos_hibernation *hibernation =
		container_of(work, struct exynos_hibernation, entry_work);

	hiber_debug(hibernation,
			"curr(%lld) entry_time(%lld)\n",
			ktime_get(), hibernation->entry_time);

	/* If hibernation entry condition does NOT meet, just return here */
	if (!exynos_hibernation_check(hibernation))
		return;

	__exynos_hibernation_handler(hibernation, true);
}

static void exynos_hibernation_exit_handler(struct kthread_work *work)
{
	struct exynos_hibernation *hibernation =
		container_of(work, struct exynos_hibernation, exit_work);

	hiber_debug(hibernation, "Display hibernation exit handler is called\n");

	__exynos_hibernation_handler(hibernation, false);
}

static ssize_t hiber_enter_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct decon_device *decon = dev_get_drvdata(dev);
	int ret = 0;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", decon->state);

	return ret;
}

static ssize_t hiber_enter_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct decon_device *decon = dev_get_drvdata(dev);
	struct exynos_drm_crtc *exynos_crtc = decon->crtc;
	bool enabled;
	int ret;

	ret = kstrtobool(buf, &enabled);
	if (ret) {
		hiber_err(exynos_crtc->hibernation, "invalid hiber enter value\n");
		return ret;
	}

	__exynos_hibernation_handler(exynos_crtc->hibernation, enabled);

	return len;
}
static DEVICE_ATTR_RW(hiber_enter);

struct exynos_hibernation *
exynos_hibernation_register(struct exynos_drm_crtc *exynos_crtc)
{
	struct decon_device *decon = exynos_crtc->ctx;
	struct device_node *np;
	struct exynos_hibernation *hibernation;
	struct device *dev = decon->dev;
	struct sched_param param;
	int ret = 0;
	u32 val = 1;

	np = dev->of_node;
	ret = of_property_read_u32(np, "hibernation", &val);
	/* if ret == -EINVAL, property is not existed */
	if (ret == -EINVAL || (ret == 0 && val == 0)) {
		pr_info("display hibernation is not supported\n");
		return NULL;
	}

	if (decon->emul_mode)
		return NULL;

	hibernation = devm_kzalloc(dev, sizeof(struct exynos_hibernation),
			GFP_KERNEL);
	if (!hibernation)
		return NULL;

	hibernation->decon = decon;
	hibernation->id = decon->id;

	mutex_init(&hibernation->lock);

	atomic_set(&hibernation->block_cnt, 0);

	kthread_init_work(&hibernation->entry_work, exynos_hibernation_handler);

	dpu_hiber_enable_mask |=  (1 << hibernation->id);
	/* initialize hibernation thread */
	kthread_init_worker(&hibernation->worker);
	hibernation->thread = kthread_run(kthread_worker_fn,
			&hibernation->worker, "exynos_hiber%d", decon->id);
	if (IS_ERR(hibernation->thread)) {
		hibernation->thread = NULL;
		hiber_err(hibernation, "failed to run hibernation thread\n");
		return NULL;
	}
	param.sched_priority = 20;
	sched_setscheduler_nocheck(hibernation->thread, SCHED_FIFO, &param);
	kthread_init_work(&hibernation->exit_work, exynos_hibernation_exit_handler);
	hibernation->early_wakeup_cnt = 0;
	ret = device_create_file(decon->dev, &dev_attr_hiber_exit);
	ret = device_create_file(decon->dev, &dev_attr_hiber_enter);
	if (ret) {
		hiber_err(hibernation, "failed to create hiber file\n");
		return NULL;
	}

	hibernation->entry_time = 0;
	hiber_info(hibernation, "display hibernation is supported\n");

	return hibernation;
}
