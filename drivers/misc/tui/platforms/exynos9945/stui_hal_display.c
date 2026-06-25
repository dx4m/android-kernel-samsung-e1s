/*
 * Samsung TUI HW Handler driver. Display functions.
 *
 * Copyright (c) 2021 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "../../stui_core.h"
#include "../../stui_hal.h"
#include "../../stui_log.h"
#include <linux/input/stui_inf.h>

#include <linux/version.h>
#ifdef CONFIG_DMA_SHARED_BUFFER
#include <linux/dma-buf.h>
#if KERNEL_VERSION(5, 17, 0) <= LINUX_VERSION_CODE
#include <linux/module.h>
	MODULE_IMPORT_NS(DMA_BUF);
#endif
#endif

#include <linux/dma-heap.h>
#include <linux/fb.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#if defined(CONFIG_EXYNOS_DPU_USE_DUAL_DRV)
#include "../../../../drivers/gpu/drm/samsung/dpu_dual/exynos_drm_tui.h"
#else
#include "../../../../drivers/gpu/drm/samsung/dpu/exynos_drm_tui.h"
#endif

#define ION_EXYNOS_FLAG_PROTECTED       (1 << 16)

extern int decon_tui_protection(bool tui_en);

static struct dma_buf *g_dma_buf;
struct dma_buf_attachment *g_attachment;
struct sg_table	*g_sgt;
struct dma_heap *dma_heap;

extern struct stui_buf_info g_stui_buf_info;
extern struct device *dev_tui;

#define DEFAULT_BPP 32

/* Find suitable framebuffer device driver */
static struct device *get_fb_dev_for_tui(void)
{
	struct device *fb_dev;

	/* get the first framebuffer device */
	fb_dev = dev_tui;

	return fb_dev;
}

static int fb_protection_for_tui(bool tui_en)
{
	struct device *fb_dev;
	int ret = 0;

	STUI_CALL_TRACE();
	log_debug("tui_en=%d\n", tui_en);

	fb_dev = get_fb_dev_for_tui();
	if (!fb_dev)
		return -1;

	if (tui_en) {
		ret = exynos_atomic_enter_tui();
		if (ret)
			log_error("protect error - %d\n", ret);
	} else {
		ret = exynos_atomic_exit_tui();
		if (ret)
			log_error("unprotect error - %d\n", ret);
	}

	exynos_tui_set_stui_funcs(stui_get_buf_info, stui_free_video_space);
	return ret;
}

void stui_free_video_space(void)
{
	STUI_CALL_TRACE();
	if (g_attachment && g_sgt) {
		dma_buf_unmap_attachment(g_attachment, g_sgt, DMA_BIDIRECTIONAL);
		g_sgt = NULL;
	}
	if (g_dma_buf && g_attachment) {
		dma_buf_detach(g_dma_buf, g_attachment);
		g_attachment = NULL;
	}
	if (g_dma_buf) {
		dma_buf_put(g_dma_buf);
		g_dma_buf = NULL;
	}
}

int stui_alloc_video_space(struct tui_hw_buffer *buffer)
{
	dma_addr_t phys_addr = 0;
	size_t framebuf_size;
	size_t workbuf_size;
	struct resolution_info lcd_info = {};

	STUI_CALL_TRACE();
	exynos_tui_get_resolution(&lcd_info);

	log_info(" resolution %d * %d,mode %d\n", lcd_info.xres, lcd_info.yres, lcd_info.mode);
	framebuf_size = (lcd_info.xres * lcd_info.yres * (DEFAULT_BPP >> 3));
	framebuf_size = STUI_ALIGN_UP(framebuf_size, STUI_ALIGN_4kB_SZ);
	workbuf_size = (lcd_info.xres * lcd_info.yres * ((DEFAULT_BPP >> 3) + 1));
	workbuf_size = STUI_ALIGN_UP(workbuf_size, STUI_ALIGN_4kB_SZ);

	dma_heap = dma_heap_find("tui-secure");
	if (!dma_heap) {
		log_error("fail to get dma_heap for tui\n");
		goto err_alloc;
	}

	g_dma_buf = dma_heap_buffer_alloc(dma_heap, framebuf_size + workbuf_size + STUI_ALIGN_4kB_SZ, 0, 0);
	if (IS_ERR(g_dma_buf)) {
		log_error("fail to allocate dma buffer\n");
		goto err_alloc;
	}

	g_attachment = dma_buf_attach(g_dma_buf, dev_tui);
	if (IS_ERR_OR_NULL(g_attachment)) {
		log_error(" fail to dma buf attachment\n");
		goto err_attach;
	}

	g_sgt = dma_buf_map_attachment(g_attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(g_sgt)) {
		log_error(" fail to map attachment\n");
		goto err_attachment;
	}

	log_info("xres=%d, yres=%d, mode=%d\n", lcd_info.xres, lcd_info.yres, lcd_info.mode);

	phys_addr = sg_phys(g_sgt->sgl);
	phys_addr = STUI_ALIGN_UP(phys_addr, STUI_ALIGN_4kB_SZ);

	buffer->width = lcd_info.xres;
	buffer->height = lcd_info.yres;
	buffer->fb_physical = (uint64_t)phys_addr;
	buffer->wb_physical = (uint64_t)((workbuf_size) ? (phys_addr + framebuf_size) : 0);
	buffer->fb_size = framebuf_size;
	buffer->wb_size = workbuf_size;
	buffer->disp_if = lcd_info.mode;
#ifdef CONFIG_EXYNOS_DPU_USE_DUAL_DRV
	buffer->disp_if &= DISP_IF_MASK;
	buffer->disp_if |= DISP_FLAG_PACK(lcd_info.disp_flag);
	log_info("xres=%d, yres=%d, mode=%d, disp_flag=%08x\n",
		lcd_info.xres, lcd_info.yres, lcd_info.mode, lcd_info.disp_flag);
#endif //CONFIG_EXYNOS_DPU_USE_DUAL_DRV
	g_stui_buf_info.pa[0] = buffer->fb_physical;
	g_stui_buf_info.pa[1] = buffer->wb_physical;
	g_stui_buf_info.pa[2] = 0;
	g_stui_buf_info.size[0] = buffer->fb_size;
	g_stui_buf_info.size[1] = buffer->wb_size;
	g_stui_buf_info.size[2] = 0;

	return 0;

err_attachment:
	dma_buf_detach(g_dma_buf, g_attachment);
err_attach:
	dma_buf_put(g_dma_buf);
err_alloc:
	return -ENOMEM;
}

int stui_get_resolution(struct tui_hw_buffer *buffer)
{
	struct resolution_info lcd_info;

	exynos_tui_get_resolution(&lcd_info);

	buffer->width = lcd_info.xres;
	buffer->height = lcd_info.yres;
	buffer->disp_if = lcd_info.mode;
#ifdef CONFIG_EXYNOS_DPU_USE_DUAL_DRV
	buffer->disp_if &= DISP_IF_MASK;
	buffer->disp_if |= DISP_FLAG_PACK(lcd_info.disp_flag);
	log_info("xres=%d, yres=%d, mode=%d, disp_flag=%08x\n",
		lcd_info.xres, lcd_info.yres, lcd_info.mode, lcd_info.disp_flag);
#endif //CONFIG_EXYNOS_DPU_USE_DUAL_DRV
	return 0;
}

int stui_prepare_tui(void)
{
	STUI_CALL_TRACE();
	return fb_protection_for_tui(true);
}

void stui_finish_tui(void)
{
	STUI_CALL_TRACE();
	if (fb_protection_for_tui(false))
		log_error(" failed to unprotect tui\n");
}

struct stui_buf_info *stui_get_buf_info(void)
{
	return &g_stui_buf_info;
}

int stui_get_lcd_info(uint64_t *lcd_buf, int size)
{
	int ret = 0;
	unsigned int i;

	STUI_CALL_TRACE();
	ret = exynos_tui_get_panel_info(lcd_buf, size);
	if (!ret) {
		for (i = 0; i < size; i++)
			log_info("lcd info[%d] = %lld\n", i, lcd_buf[i]);
	}
	return ret;
}
