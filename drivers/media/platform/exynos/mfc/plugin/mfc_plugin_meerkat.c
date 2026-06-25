/*
 * drivers/media/platform/exynos/mfc/plugin/mfc_plugin_meerkat.c
 *
 * Copyright (c) 2022 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mfc_plugin_meerkat.h"
#include "mfc_plugin_pm.h"

#include "base/mfc_regs.h"
#include "base/mfc_queue.h"
#include "base/mfc_utils.h"

#define FG_SFR_AREA_COUNT	4

static void __mfc_plugin_dump_regs(struct mfc_core *core)
{
	int i, clk_enable = 0;
	int addr[FG_SFR_AREA_COUNT][2] = {
		{ REG_FG_PICTURE_START, 0x110 },
		{ REG_FG_RDMA_ENABLE, 0x1DC },
		{ REG_FG_WDMA_LUM_ENABLE, 0x1DC },
		{ REG_FG_WDMA_CHR_ENABLE, 0x1DC },
	};
	int start, offset = 0xD4;

	dev_err(core->device, "-----------dumping Filmgrain registers\n");

	if (!mfc_core_get_pwr_ref_cnt(core)) {
		dev_err(core->device, "FG Power(%d) is not enabled\n",
				mfc_core_get_pwr_ref_cnt(core));
		return;
	}

	if (!mfc_core_get_clk_ref_cnt(core)) {
		dev_info(core->device, "Filmgrain clock(%d) is not enabled\n",
				mfc_core_get_clk_ref_cnt(core));
		mfc_plugin_pm_clock_on(core);
		clk_enable = 1;
	}

	for (i = 0; i < FG_SFR_AREA_COUNT; i++) {
		dev_err(core->device, "[%04X .. %04X]\n", addr[i][0], addr[i][0] + addr[i][1]);
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 32, 4,
				core->regs_base + addr[i][0], addr[i][1], false);
		dev_err(core->device, "...\n");
	}

	if (core->dev->pdata->support_fg_shadow) {
		for (i = 1 ; i < MFC_FG_NUM_SHADOW_REGS; i++) {
			start = REG_FG_PICTURE_START + (i * 4096);

			dev_err(core->device, "[%04X .. %04X]\n", start, start + offset);
			print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 32, 4,
				core->regs_base + start, offset, false);
			dev_err(core->device, "...\n");
		}

		offset = 0x100;
		for (i = 1; i < FG_SFR_AREA_COUNT; i++) {
			start = addr[i][0] + 0x200;
			dev_err(core->device, "[%04X .. %04X]\n", start, start + offset);
			print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 32, 4,
					core->regs_base + start, offset, false);
			dev_err(core->device, "...\n");
		}
	}

	if (clk_enable)
		mfc_plugin_pm_clock_off(core);
}

static void __mfc_plugin_dump_trace(struct mfc_core *core)
{
	struct mfc_dev *dev = core->dev;
	int i, cnt, trace_cnt;
	int trace_count_print = MFC_TRACE_COUNT_PRINT;

	if (!dev->pdata->debug_mode && !dev->debugfs.debug_mode_en)
		trace_count_print = MFC_TRACE_COUNT_PRINT_LONG;

	dev_err(core->device, "-----------dumping MFC trace info-----------\n");

	trace_cnt = atomic_read(&dev->trace_ref);
	for (i = trace_count_print - 1; i >= 0; i--) {
		cnt = ((trace_cnt + MFC_TRACE_COUNT_MAX) - i) % MFC_TRACE_COUNT_MAX;
		if (dev->mfc_trace[cnt].time == 0)
			continue;
		dev_err(core->device, "MFC trace[%d]: time=%llu, str=%s", cnt,
				dev->mfc_trace[cnt].time, dev->mfc_trace[cnt].str);
	}

	dev_err(core->device, "-----------dumping MFC RM trace info-----------\n");

	trace_cnt = atomic_read(&dev->trace_ref_rm);
	for (i = trace_count_print - 1; i >= 0; i--) {
		cnt = ((trace_cnt + MFC_TRACE_COUNT_MAX) - i) % MFC_TRACE_COUNT_MAX;
		if (dev->mfc_trace_rm[cnt].time == 0)
			continue;
		dev_err(core->device, "MFC RM trace[%d]: time=%llu, str=%s", cnt,
				dev->mfc_trace_rm[cnt].time, dev->mfc_trace_rm[cnt].str);
	}
}

static void __mfc_plugin_dump_info_without_regs(struct mfc_core *core)
{
	struct mfc_core_ctx *core_ctx = core->core_ctx[core->curr_core_ctx];
	struct mfc_ctx *ctx = core_ctx->ctx;
	struct mfc_dec *dec = ctx->dec_priv;
	struct mfc_buf *mfc_buf = NULL;
	int i;

	dev_err(core->device, "-----------dumping Film grain info-----------\n");
	dev_err(core->device, "power(FG):%d, clock:%d, num_inst:%d, num_drm_inst:%d\n",
			mfc_core_get_pwr_ref_cnt(core),
			mfc_core_get_clk_ref_cnt(core),
			core->num_inst, core->num_drm_inst);
	dev_err(core->device, "hwlock bits:%#lx, curr_ctx:%d, work_bits:%#lx\n",
			core->hwlock.bits, core->curr_core_ctx, mfc_get_bits(&core->work_bits));

	if (core->dev->pdata->support_fg_shadow) {
		dev_err(core->device,
			"HW queue %s - queue_cnt: %d, exe_cnt: %d\n",
			core->fg_q_enable ? "enabled" : "disabled",
			core->fg_q_handle->queue_count,
			core->fg_q_handle->exe_count);

		for (i = 0; i < MFC_FG_NUM_SHADOW_REGS; i++)
			dev_err(core->device, "ctx_num_table[%d] = %d\n",
				i, core->fg_q_handle->ctx_num_table[i]);
	}

	dev_err(core->device, "-----------dumping Filmgrain queue-----------\n");
	if (!list_empty(&ctx->plugin_buf_queue.head))
		list_for_each_entry(mfc_buf, &ctx->plugin_buf_queue.head, list)
			dev_err(core->device, "plugin_buf src[%d] %#llx, dst[%d] %#llx, used: %d\n",
				mfc_buf->dpb_index, dec->dpb[mfc_buf->dpb_index].addr[0],
				mfc_buf->vb.vb2_buf.index, mfc_buf->addr[0][0], mfc_buf->used);
}

static void __mfc_plugin_dump_info(struct mfc_core *core)
{
	mfc_dump_state(core->dev);
	__mfc_plugin_dump_trace(core);
	__mfc_plugin_dump_info_without_regs(core);
	__mfc_plugin_dump_regs(core);
}

static void __mfc_plugin_dump_info_and_stop_hw(struct mfc_core *core)
{
	__mfc_plugin_dump_info(core);

	MFC_TRACE_CORE("** %s will stop!!!\n", core->name);
	dev_err(core->device, "** %s will stop!!!\n", core->name);

	dbg_snapshot_expire_watchdog();
	BUG();
}

static void __mfc_plugin_dump_info_and_stop_hw_debug_mode(struct mfc_core *core)
{
	struct mfc_dev *dev = core->dev;

	__mfc_plugin_dump_info(core);

	if (!dev->pdata->debug_mode && !dev->debugfs.debug_mode_en)
		return;

	MFC_TRACE_CORE("** %s will stop!!!\n", core->name);
	dev_err(core->device, "** %s will stop!!!\n", core->name);

	dbg_snapshot_expire_watchdog();
	BUG();
}

struct mfc_core_dump_ops mfc_plugin_dump_ops = {
	.dump_regs			= __mfc_plugin_dump_regs,
	.dump_info			= __mfc_plugin_dump_info,
	.dump_info_without_regs		= __mfc_plugin_dump_info_without_regs,
	.dump_and_stop_always		= __mfc_plugin_dump_info_and_stop_hw,
	.dump_and_stop_debug_mode	= __mfc_plugin_dump_info_and_stop_hw_debug_mode,
};
