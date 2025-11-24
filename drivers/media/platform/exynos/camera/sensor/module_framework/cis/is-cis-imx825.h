/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Samsung Exynos SoC series Sensor driver
 *
 *
 * Copyright (c) 2025 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IS_CIS_IMX825_H
#define IS_CIS_IMX825_H

#include "is-cis.h"

#define AEB_IMX825_LUT0	0x0E20
#define AEB_IMX825_LUT1	0x0E40

#define AEB_IMX825_OFFSET_CIT		0x0
#define AEB_IMX825_OFFSET_AGAIN		0x2
#define AEB_IMX825_OFFSET_DGAIN		0x4
#define AEB_IMX825_OFFSET_FLL		0x8

static const struct sensor_reg_addr sensor_imx825_reg_addr = {
	.fll = 0x0340,
	.fll_aeb_long = AEB_IMX825_LUT0 + AEB_IMX825_OFFSET_FLL,
	.fll_aeb_short = AEB_IMX825_LUT1 + AEB_IMX825_OFFSET_FLL,
	.fll_shifter = 0x3161,
	.cit = 0x0202,
	.cit_aeb_long = AEB_IMX825_LUT0 + AEB_IMX825_OFFSET_CIT,
	.cit_aeb_short = AEB_IMX825_LUT1 + AEB_IMX825_OFFSET_CIT,
	.cit_shifter = 0x3160,
	.again = 0x0204,
	.again_aeb_long = AEB_IMX825_LUT0 + AEB_IMX825_OFFSET_AGAIN,
	.again_aeb_short = AEB_IMX825_LUT1 + AEB_IMX825_OFFSET_AGAIN,
	.dgain = 0x020E,
	.dgain_aeb_long = AEB_IMX825_LUT0 + AEB_IMX825_OFFSET_DGAIN,
	.dgain_aeb_short = AEB_IMX825_LUT1 + AEB_IMX825_OFFSET_DGAIN,
	.group_param_hold = 0x0104,
};

#define MODE_GROUP_NONE (-1)
enum sensor_imx825_mode_group_enum {
	SENSOR_IMX825_MODE_DEFAULT,
	SENSOR_IMX825_MODE_AEB,
	SENSOR_IMX825_MODE_IDCG,
	SENSOR_IMX825_MODE_LN2,
	SENSOR_IMX825_MODE_LN4,
	SENSOR_IMX825_MODE_GROUP_MAX
};

struct sensor_imx825_private_runtime {
	u32 lte_on_delay;
	bool is_front;
	u32 mode_groups[SENSOR_IMX825_MODE_GROUP_MAX];
};

struct sensor_imx825_private_data {
	const struct sensor_regs global;
	const struct sensor_regs init_aeb;
};

/**
 * Register address & data
 */
#define DATA_IMX825_GPH_HOLD            (0x01)
#define DATA_IMX825_GPH_RELEASE         (0x00)

int sensor_imx825_cis_stream_on(struct v4l2_subdev *subdev);
int sensor_imx825_cis_stream_off(struct v4l2_subdev *subdev);
int sensor_imx825_cis_update_seamless_mode(struct v4l2_subdev *subdev);
#endif
