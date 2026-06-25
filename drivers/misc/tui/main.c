/* tui/main.c
 *
 * Samsung TUI HW Handler driver.
 *
 * Copyright (c) 2015 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include "linux/of.h"
#include "linux/miscdevice.h"
#include <linux/dma-mapping.h>
#if defined(CONFIG_SOC_S5E9925) || defined(CONFIG_SOC_S5E8825)
#include <linux/dma-map-ops.h>
#endif

#include "stui_core.h"
#include "stui_hal.h"
#include "stui_inf.h"
#include "stui_ioctl.h"
#include "stui_log.h"
#ifdef CONFIG_SAMSUNG_TUI_LOWLEVEL
#include "iwd_agent.h"
#endif /* CONFIG_SAMSUNG_TUI_LOWLEVEL */

static DEFINE_MUTEX(stui_mode_mutex);
struct device* (dev_tui) = NULL;
struct miscdevice tui;

static void stui_wq_func(struct work_struct *param)
{
	struct delayed_work *wq = container_of(param, struct delayed_work, work);
	STUI_CALL_TRACE();
	mutex_lock(&stui_mode_mutex);
	stui_close_touch();
	stui_close_display();
	kfree(wq);
	mutex_unlock(&stui_mode_mutex);
}

static int stui_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	STUI_CALL_TRACE();
	mutex_lock(&stui_mode_mutex);
	filp->private_data = NULL;
	if (stui_get_mode() & STUI_MODE_ALL) {
		ret = -EBUSY;
		log_error("Device is busy\n");
	}
	mutex_unlock(&stui_mode_mutex);
	return ret;
}

static int stui_release(struct inode *inode, struct file *filp)
{
	struct delayed_work *work;
	STUI_CALL_TRACE();
	mutex_lock(&stui_mode_mutex);
	if ((stui_get_mode() & STUI_MODE_ALL) && filp->private_data) {
		log_error("Device close while TUI session is active\n");
		work = kmalloc(sizeof(struct delayed_work), GFP_KERNEL);
		if (!work) {
			mutex_unlock(&stui_mode_mutex);
			log_error("memory allocation error\n");
			return -ENOMEM;
		}
		INIT_DELAYED_WORK(work, stui_wq_func);
		schedule_delayed_work(work, msecs_to_jiffies(4000));
	}
	mutex_unlock(&stui_mode_mutex);
	return 0;
}

static long stui_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	long ret;
	STUI_CALL_TRACE();
	mutex_lock(&stui_mode_mutex);
	ret = stui_process_cmd(f, cmd, arg);
	if (stui_get_mode() & STUI_MODE_ALL)
		f->private_data = (void *)1UL;
	else
		f->private_data = (void *)0UL;
	mutex_unlock(&stui_mode_mutex);
	log_debug("ret=%ld\n", ret);
	return ret;
}

static int exynos_teegris_tui_probe(struct platform_device *pdev)
{
	STUI_CALL_TRACE();
	(void)pdev;
	dev_tui = &pdev->dev;
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	dma_set_mask(&pdev->dev, DMA_BIT_MASK(36));
	return 0;
}

static const struct of_device_id exynos_teegris_tui_of_match_table[] = {
	{ .compatible = "samsung,exynos-tui", },
	{ },
};

static struct platform_driver exynos_teegris_tui_driver = {
	.probe = exynos_teegris_tui_probe,
	.driver = {
		.name = "exynos-tui",
		.owner = THIS_MODULE,
		.of_match_table = exynos_teegris_tui_of_match_table,
	}
};

static int __init teegris_tui_init(void)
{
	int ret;

	log_info(" =============== Running TEEgris TUI  ===============\n");

	ret = misc_register(&tui);
	if (ret) {
		log_error(" tui can't register misc on minor=%d\n",
				MISC_DYNAMIC_MINOR);
		return ret;
	}

#ifdef CONFIG_SAMSUNG_TUI_LOWLEVEL
	__init_iwd_agent();
	init_iwd_agent();
#endif /* CONFIG_SAMSUNG_TUI_LOWLEVEL */

	return platform_driver_register(&exynos_teegris_tui_driver);
}

static void __exit teegris_tui_exit(void)
{
	log_info("Unloading teegris tui module.\n");
#ifdef CONFIG_SAMSUNG_TUI_LOWLEVEL
	uninit_iwd_agent();
	__uninit_iwd_agent();
#endif /* CONFIG_SAMSUNG_TUI_LOWLEVEL */
	misc_deregister(&tui);
	platform_driver_unregister(&exynos_teegris_tui_driver);
}

static const struct file_operations tui_fops = {
		.owner = THIS_MODULE,
		.unlocked_ioctl = stui_ioctl,
#ifdef CONFIG_COMPAT
		.compat_ioctl = stui_ioctl,
#endif
		.open = stui_open,
		.release = stui_release,
};

struct miscdevice tui = {
		.minor  = MISC_DYNAMIC_MINOR,
		.name   = STUI_DEV_NAME,
		.fops   = &tui_fops,
};

module_init(teegris_tui_init);
module_exit(teegris_tui_exit);

MODULE_AUTHOR("TUI Teegris");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TEEGRIS TUI");
