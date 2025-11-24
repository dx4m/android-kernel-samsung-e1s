/*
 * Samsung Exynos SoC series NPU driver
 *
 * Copyright (c) 2017 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _NPU_STATE_H_
#define _NPU_STATE_H_

#include <soc/samsung/exynos_pm_qos.h>

#include "npu-device.h"

#define NPU_MAX_NODE_NUM	(4)
#define NPU_MAX_FREQ_NODE_NUM (16)

struct npu_state_freq_info {
	u32 dnc;
	u32 npu0;
#if IS_ENABLED(CONFIG_SOC_S5E9945)
	u32 npu1;
	u32 dsp;
#endif
};

struct npu_state {
	u32 cur_state;
	u32 node_num;
	ssize_t node_count;
	const char *node_name[NPU_MAX_NODE_NUM];
	struct exynos_pm_qos_request	npu_state_dnc_max;
	struct exynos_pm_qos_request	npu_state_npu0_max;
	struct exynos_pm_qos_request	npu_state_npu1_max;
	struct exynos_pm_qos_request	npu_state_dsp_max;
	struct npu_state_freq_info *npu_freq_level;
	u32 *time_in_state;
};

int npu_state_sysfs_create(struct npu_system *system);
int npu_state_update_time_in_state(u32 execution_time);
void npu_state_set_start_time(struct npu_session *session);
int npu_state_set_execution_time(struct npu_queue *queue);
#endif
