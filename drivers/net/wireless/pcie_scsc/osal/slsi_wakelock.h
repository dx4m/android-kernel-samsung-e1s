/****************************************************************************
 *
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef _SLSI_WAKELOCK_H
#define _SLSI_WAKELOCK_H

#include <linux/ktime.h>
#include <linux/device.h>

struct slsi_wake_lock {
	struct wakeup_source *ws;
};

#ifdef CONFIG_SCSC_WLAN_ANDROID
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static inline void slsi_wake_lock_init(struct device *dev, struct slsi_wake_lock *lock, const char *name)
{
	lock->ws = wakeup_source_register(dev, name);
}
#else
static inline void slsi_wake_lock_init(struct device *dev, struct slsi_wake_lock *lock, const char *name)
{
	wakeup_source_init(lock->ws, name);
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static inline void slsi_wake_lock_destroy(struct slsi_wake_lock *lock)
{
	wakeup_source_unregister(lock->ws);
}
#else
static inline void slsi_wake_lock_destroy(struct slsi_wake_lock *lock)
{
	wakeup_source_trash(lock->ws);
}
#endif

static inline void slsi_wake_lock(struct slsi_wake_lock *lock)
{
	__pm_stay_awake(lock->ws);
}

static inline void slsi_wake_lock_timeout(struct slsi_wake_lock *lock, long timeout)
{
	__pm_wakeup_event(lock->ws, jiffies_to_msecs(timeout));
}

static inline void slsi_wake_unlock(struct slsi_wake_lock *lock)
{
	__pm_relax(lock->ws);
}

static inline int slsi_wake_lock_active(struct slsi_wake_lock *lock)
{
	return lock->ws->active;
}

#else
#define slsi_wake_lock_init(lock, type, name)
#define slsi_wake_lock(lock)
#define slsi_wake_unlock(lock)
#define slsi_wake_lock_timeout(lock, timeout)
#define slsi_wake_lock_active(lock)				(false)
#define slsi_wake_lock_destroy(lock)
#endif
#endif

