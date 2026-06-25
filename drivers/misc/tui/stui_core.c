/* tui/stui_core.c
 *
 * Samsung TUI HW Handler driver.
 *
 * Copyright (C) 2012, Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>

#include "stui_core.h"
#include "stui_hal.h"
#include "stui_inf.h"
#include "stui_log.h"

#include <linux/smc.h>

struct stui_buf_info g_stui_buf_info;
uint32_t g_stui_disp_if;

#ifdef SAMSUNG_TUI_TEST
static uint64_t g_fb_pa;

static void stui_write_signature(void)
{
	uint32_t *kaddr;
	struct page *page;

	page = pfn_to_page(g_fb_pa >> PAGE_SHIFT);
	kaddr = kmap(page);
	if (kaddr) {
		*kaddr = 0x01020304;
		log_debug("kaddr : %pK %x\n", kaddr, *kaddr);
		kunmap(page);
	} else
		log_error("kmap failed\n");
}
#endif


#define SMC_DRM_TUI_PROT		(0x82002120)
#define SMC_DRM_TUI_UNPROT		(0x82002121)

long stui_process_cmd(struct file *f, unsigned int cmd, unsigned long arg)
{
	long ret = 0;

	/* Handle command */
	STUI_CALL_TRACE();
	switch (cmd) {
	case STUI_HW_IOCTL_START_TUI: {
		struct tui_hw_buffer __user *argp = (struct tui_hw_buffer __user *)arg;
		struct tui_hw_buffer buffer;

		log_info("STUI_HW_IOCTL_START_TUI called\n");

		if (stui_get_mode() & STUI_MODE_ALL) {
			ret = -EBUSY;
			break;
		}

		ret = stui_open_touch();
		if (ret < 0) {
			log_error("stui_open_touch failed\n");
			goto lbl_rollback_touch;
		}

		g_stui_disp_if = 0;
		ret = stui_open_display(&buffer);
		if (ret < 0) {
			log_error("stui_open_display failed\n");
			goto lbl_rollback_display;
		}

		buffer.touch_type = stui_get_touch_type();
		log_info("stui tsp_type=%d, buffer.disp_if=%x\n", buffer.touch_type, buffer.disp_if);
#ifdef CONFIG_EXYNOS_DPU_USE_DUAL_DRV
		log_info("stui disp_flag=%08x\n", DISP_FLAG_GET(buffer.disp_if));
#endif //CONFIG_EXYNOS_DPU_USE_DUAL_DRV
		ret = stui_get_lcd_info(buffer.lcd_info, STUI_DISPLAY_INFO_SIZE);
		if (ret < 0) {
			log_error("failed to get lcd info\n");
			goto lbl_rollback_display;
		}

		g_stui_disp_if = buffer.disp_if & DISP_IF_MASK;

		if (copy_to_user(argp, &buffer, sizeof(struct tui_hw_buffer))) {
			log_error("copy_to_user failed\n");
			ret = -EFAULT;
			goto lbl_rollback_display;
		}
		stui_set_tui_version(TUI_OLD);
		break;

lbl_rollback_display:
		stui_close_display();
lbl_rollback_touch:
		stui_close_touch();
		break;
	}
	case STUI_HW_IOCTL_FINISH_TUI: {
		log_info("STUI_HW_IOCTL_FINISH_TUI called\n");
		if (stui_get_mode() == STUI_MODE_OFF) {
			log_error("stui mode = STUI_MODE_OFF\n");
			ret = -EPERM;
			break;
		}
		stui_close_display();
		stui_close_touch();
		stui_set_mode(STUI_MODE_OFF);
		stui_set_tui_version(TUI_NOPE);
		break;
	}
#ifdef SAMSUNG_TUI_TEST
	case STUI_HW_IOCTL_GET_PHYS_ADDR: {
		uint64_t __user *argp = (uint64_t __user *)arg;

		if (copy_to_user(argp, &g_fb_pa, sizeof(uint64_t))) {
			log_error("copy_to_user failed\n");
			ret = -EFAULT;
		}
		break;
	}
#endif
	case STUI_HW_IOCTL_GET_RESOLUTION: {
		struct tui_hw_buffer __user *argp = (struct tui_hw_buffer __user *)arg;
		struct tui_hw_buffer buffer;

		log_info("TUI_HW_IOCTL_GET_RESOLUTION called\n");
		memset(&buffer, 0, sizeof(struct tui_hw_buffer));
		if (stui_get_resolution(&buffer)) {
			log_error("stui_get_resolution failed\n");
			ret = -EPERM;
			break;
		}

		buffer.touch_type = stui_get_touch_type();

#ifdef CONFIG_EXYNOS_DPU_USE_DUAL_DRV
		log_debug("width=%d, height=%d, touch_type=%d, disp_flag=%08x\n",
			buffer.width, buffer.height, buffer.touch_type,
			DISP_FLAG_GET(buffer.disp_if));
#endif //CONFIG_EXYNOS_DPU_USE_DUAL_DRV

		if (copy_to_user(argp, &buffer, sizeof(struct tui_hw_buffer))) {
			log_error("copy_to_user failed\n");
			ret = -EFAULT;
		}
		break;
	}
	default:
		log_error("Unknown command %d\n", cmd);
		ret = -ENOTTY;
		break;
	}
	log_debug("ret=%ld\n", ret);
	return ret;
}

int stui_open_touch(void)
{
	STUI_CALL_TRACE();

	if (stui_get_mode() & STUI_MODE_TOUCH_SEC) {
		log_error("already in TUI mode.\n");
		return -EBUSY;
	}

	if (stui_i2c_protect(true) != 0) {
		log_error("stui_i2c_protect failed.\n");
		return -EPERM;
	}
	stui_set_mask(STUI_MODE_TOUCH_SEC);

	return 0;
}

int stui_open_display(struct tui_hw_buffer *buffer)
{
	STUI_CALL_TRACE();
	g_stui_disp_if = 0;

	if (stui_get_mode() & STUI_MODE_DISPLAY_SEC) {
		log_error("already in TUI mode.\n");
		return -EBUSY;
	}

	/* allocate TUI frame buffer */
	log_info("Allocating Framebuffer\n");
	memset(buffer, 0, sizeof(struct tui_hw_buffer));
	if (stui_alloc_video_space(buffer)) {
		log_error("stui_alloc_video_space failed.\n");
		return -EPERM;
	}

#ifdef CONFIG_EXYNOS_DPU_USE_DUAL_DRV
	log_debug("disp_flag=%08x, disp_if=%08x\n",
		DISP_FLAG_GET(buffer->disp_if),
		buffer->disp_if & DISP_IF_MASK);
#endif //CONFIG_EXYNOS_DPU_USE_DUAL_DRV
#ifdef SAMSUNG_TUI_TEST
	g_fb_pa = buffer.fb_physical;
	stui_write_signature();
#endif

	/* Prepare display for TUI / Deactivate linux UI drivers */
	if (stui_prepare_tui()) {
		log_error("stui_prepare_tui failed.\n");
		stui_free_video_space();
		return -EFAULT;
	}

	stui_set_mask(STUI_MODE_DISPLAY_SEC);
	return 0;
}

void stui_close_touch(void)
{
	STUI_CALL_TRACE();
	if ((stui_get_mode() & STUI_MODE_TOUCH_SEC) == 0) {
		log_error("already free.\n");
		return;
	}

	stui_i2c_protect(false);
	stui_clear_mask(STUI_MODE_TOUCH_SEC);
}

void stui_close_display(void)
{
	STUI_CALL_TRACE();
	if ((stui_get_mode() & STUI_MODE_DISPLAY_SEC) == 0) {
		log_error("already free.\n");
		return;
	}
	/* Disable STUI driver / Activate linux UI drivers */
	stui_clear_mask(STUI_MODE_DISPLAY_SEC);
	stui_finish_tui();
	log_info("Freeing Framebuffer\n");
	if (!g_stui_disp_if)
		stui_free_video_space();
}

int __attribute__((weak)) stui_get_lcd_info(uint64_t *lcd_buf, int size)
{
	(void)lcd_buf;
	(void)size;
	return 0;
}
