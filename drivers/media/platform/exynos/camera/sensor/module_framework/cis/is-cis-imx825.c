// SPDX-License-Identifier: GPL-2.0-or-later
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

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <videodev2_exynos_camera.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <exynos-is-sensor.h>

#include "is-hw.h"
#include "is-core.h"
#include "is-param.h"
#include "is-device-sensor.h"
#include "is-device-sensor-peri.h"
#include "is-resourcemgr.h"
#include "is-dt.h"
#include "is-cis-imx825.h"
#include "is-cis-imx825-setA-19p2.h"
#include "is-helper-ixc.h"

#define SENSOR_NAME "IMX825"

u32 sensor_imx825_cis_calc_again_code(u32 permille)
{
	return 16384 - (16384000 / permille);
}

u32 sensor_imx825_cis_calc_again_permile(u32 code)
{
	return 16384000 / (16384 - code);
}

void sensor_imx825_cis_set_mode_group(struct v4l2_subdev *subdev, u32 mode)
{
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	struct sensor_imx825_private_runtime *priv_runtime = (struct sensor_imx825_private_runtime *)cis->priv_runtime;

	priv_runtime->mode_groups[SENSOR_IMX825_MODE_DEFAULT] = mode;
	priv_runtime->mode_groups[SENSOR_IMX825_MODE_AEB] = MODE_GROUP_NONE;
	priv_runtime->mode_groups[SENSOR_IMX825_MODE_IDCG] = MODE_GROUP_NONE;
	priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN2] = MODE_GROUP_NONE;
	priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4] = MODE_GROUP_NONE;

	switch (mode) {
	case SENSOR_IMX825_4000X3000_60FPS_10BIT:
		priv_runtime->mode_groups[SENSOR_IMX825_MODE_AEB] = SENSOR_IMX825_4000X3000_60FPS_10BIT;
		priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4] = SENSOR_IMX825_4000X3000_24FPS_10BIT_LN4;
		break;
	case SENSOR_IMX825_4000X3000_30FPS_12BIT_AD:
		priv_runtime->mode_groups[SENSOR_IMX825_MODE_IDCG] = SENSOR_IMX825_4000X3000_30FPS_12BIT_AD;
		priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4] = SENSOR_IMX825_4000X3000_24FPS_12BIT_LN4;
		break;
	case SENSOR_IMX825_4000X2252_60FPS_10BIT:
		priv_runtime->mode_groups[SENSOR_IMX825_MODE_AEB] = SENSOR_IMX825_4000X2252_60FPS_10BIT;
		priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4] = SENSOR_IMX825_4000X2252_30FPS_10BIT_LN4;
		break;
	case SENSOR_IMX825_4000X2252_60FPS_12BIT_AD:
		priv_runtime->mode_groups[SENSOR_IMX825_MODE_IDCG] = SENSOR_IMX825_4000X2252_60FPS_12BIT_AD;
		priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4] = SENSOR_IMX825_4000X2252_30FPS_12BIT_LN4;
		break;
	default:
		break;
	}

	info("[%d][%s] default(%d) aeb(%d) idcg(%d) ln2(%d) ln4(%d)\n", cis->id, __func__,
		 priv_runtime->mode_groups[SENSOR_IMX825_MODE_DEFAULT],
		 priv_runtime->mode_groups[SENSOR_IMX825_MODE_AEB],
		 priv_runtime->mode_groups[SENSOR_IMX825_MODE_IDCG],
		 priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN2],
		 priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4]);
}

/* CIS OPS */
int sensor_imx825_cis_init(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	struct sensor_imx825_private_runtime *priv_runtime = (struct sensor_imx825_private_runtime *)cis->priv_runtime;
	ktime_t st = ktime_get();

#if !defined(CONFIG_CAMERA_VENDOR_MCD)
	memset(cis->cis_data, 0, sizeof(cis_shared_data));

	ret = sensor_cis_check_rev(cis);
	if (ret < 0) {
		warn("sensor_imx825_check_rev is fail when cis init");
		return -EINVAL;
	}
#endif

	cis->cis_data->cur_width = cis->sensor_info->max_width;
	cis->cis_data->cur_height = cis->sensor_info->max_height;
	cis->cis_data->low_expo_start = 33000;

	priv_runtime->mode_groups[SENSOR_IMX825_MODE_DEFAULT] = MODE_GROUP_NONE;
	priv_runtime->mode_groups[SENSOR_IMX825_MODE_IDCG] = MODE_GROUP_NONE;
	priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN2] = MODE_GROUP_NONE;
	priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4] = MODE_GROUP_NONE;

	cis->cis_data->sens_config_index_pre = SENSOR_IMX825_MODE_MAX;
	cis->cis_data->sens_config_index_cur = 0;
	CALL_CISOPS(cis, cis_data_calculation, subdev, cis->cis_data->sens_config_index_cur);

	if (IS_ENABLED(DEBUG_SENSOR_TIME))
		dbg_sensor(1, "[%d][%s] time %lldus", cis->id, __func__, PABLO_KTIME_US_DELTA_NOW(st));

	return ret;
}

int sensor_imx825_cis_deinit(struct v4l2_subdev *subdev)
{
	int ret = 0;

	return ret;
}

static const struct is_cis_log log_imx825[] = {
	{I2C_READ, 8, 0x0018, 0, "read_8 0x0018 revision_number"},
	{I2C_READ, 8, 0x0005, 0, "read_8 0x0005 frame_count"},
	{I2C_READ, 16, 0x0204, 0, "read_16 0x0204 analog_gain"},
	{I2C_READ, 16, 0x020E, 0, "read_16 0x020E digital_gain"},
	{I2C_READ, 8, 0x0100, 0, "read_8 0x0100 Stream on / off"},
	{I2C_READ, 16, 0x0340, 0, "read_16 0x0340 Frame Length Lines Setting"},
	{I2C_READ, 16, 0x0202, 0, "read_16 0x0202 Integration Setting"},
	{I2C_READ, 8, 0x0104, 0, "read_8 0x0104 GPH"},
};

int sensor_imx825_cis_log_status(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_cis *cis = sensor_cis_get_cis(subdev);

	sensor_cis_log_status(cis, log_imx825, ARRAY_SIZE(log_imx825), (char *)__func__);

	return ret;
}

int sensor_imx825_cis_init_aeb(struct is_cis *cis, bool aeb_support)
{
	int ret = 0;
	struct sensor_imx825_private_data *priv = (struct sensor_imx825_private_data *)cis->sensor_info->priv;

	if (aeb_support) {
		ret |= sensor_cis_write_registers(cis->subdev, priv->init_aeb);
		info("[%d][%s] init_aeb done. ret=%d", cis->id, __func__, ret);
	} else {
		ret = -EINVAL;
		err("not support 12bit aeb");
	}

	if (ret < 0)
		err("sensor_imx825_cis_init_aeb fail!! %d", ret);

	return ret;
}

int sensor_imx825_cis_aeb_on(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_cis *cis = sensor_cis_get_cis(subdev);

	info("[%d][%s] enable 2AEB 2VC\n", cis->id, __func__);
	ret |= cis->ixc_ops->write8(cis->client, 0x0E00, 0x02);

	cis->cis_data->fll_margin_ratio_aeb = 2000; /* x2 */

	return ret;
}

int sensor_imx825_cis_aeb_off(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_cis *cis = sensor_cis_get_cis(subdev);

	info("[%d][%s] disable AEB\n", cis->id, __func__);
	cis->ixc_ops->write8(cis->client, 0x0E00, 0x00);

	return ret;
}

int sensor_imx825_cis_check_aeb(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_device_sensor *device;
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	struct sensor_imx825_private_runtime *priv_runtime = (struct sensor_imx825_private_runtime *)cis->priv_runtime;

	device = (struct is_device_sensor *)v4l2_get_subdev_hostdata(subdev);
	WARN_ON(!device);

	if (cis->cis_data->stream_on == false
		|| !is_sensor_check_aeb_mode(device)
		|| priv_runtime->mode_groups[SENSOR_IMX825_MODE_AEB] == MODE_GROUP_NONE) {
		if (cis->cis_data->cur_hdr_mode == SENSOR_HDR_MODE_2AEB_2VC) {
			info("[%d][%s] current AEB status is enabled. need AEB disable\n", cis->id, __func__);
			cis->cis_data->pre_hdr_mode = SENSOR_HDR_MODE_SINGLE;
			cis->cis_data->cur_hdr_mode = SENSOR_HDR_MODE_SINGLE;
			/* AEB disable */
			cis->ixc_ops->write8(cis->client, 0x0E00, 0x00);
			info("[%d][%s] disable AEB in not support mode\n", cis->id, __func__);
		}
		return -1; //continue;
	}

	if (cis->cis_data->cur_hdr_mode == cis->cis_data->pre_hdr_mode)
		return -1; // continue;

	if (cis->cis_data->cur_hdr_mode == SENSOR_HDR_MODE_2AEB_2VC) {
		sensor_imx825_cis_aeb_on(subdev);
	} else {
		sensor_imx825_cis_aeb_off(subdev);
	}

	cis->cis_data->pre_hdr_mode = cis->cis_data->cur_hdr_mode;

	info("[%s] done\n", __func__);

	return ret;
}

int sensor_imx825_cis_check_12bit(struct v4l2_subdev *subdev, u32 *next_mode)
{
	int ret = 0;
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	cis_shared_data *cis_data = cis->cis_data;
	struct sensor_imx825_private_runtime *priv_runtime = (struct sensor_imx825_private_runtime *)cis->priv_runtime;

	if (priv_runtime->mode_groups[SENSOR_IMX825_MODE_IDCG] == MODE_GROUP_NONE)
		return -1;

	switch (cis_data->cur_12bit_mode) {
	case SENSOR_12BIT_STATE_REAL_12BIT:
		*next_mode = priv_runtime->mode_groups[SENSOR_IMX825_MODE_IDCG];
		break;
	case SENSOR_12BIT_STATE_PSEUDO_12BIT:
	default:
		ret = -1;
		break;
	}

	return ret;
}

int sensor_imx825_cis_check_lownoise(struct v4l2_subdev *subdev, u32 *next_mode)
{
	int ret = 0;
	u32 temp_mode = MODE_GROUP_NONE;
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	cis_shared_data *cis_data = cis->cis_data;
	struct sensor_imx825_private_runtime *priv_runtime = (struct sensor_imx825_private_runtime *)cis->priv_runtime;

	if (priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN2] == MODE_GROUP_NONE
		&& priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4] == MODE_GROUP_NONE)
		return -1;

	switch (cis_data->cur_lownoise_mode) {
	case IS_CIS_LN2:
		temp_mode = priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN2];
		break;
	case IS_CIS_LN4:
		temp_mode = priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4];
		break;
	case IS_CIS_LNOFF:
	default:
		break;
	}

	if (temp_mode == MODE_GROUP_NONE)
		ret = -1;

	if (ret == 0)
		*next_mode = temp_mode;

	return ret;
}

int sensor_imx825_cis_update_seamless_mode(struct v4l2_subdev *subdev)
{
	int ret = 0;
	unsigned int mode = 0;
	unsigned int next_mode = 0;
	const struct sensor_cis_mode_info *next_mode_info;
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	struct sensor_imx825_private_runtime *priv_runtime = (struct sensor_imx825_private_runtime *)cis->priv_runtime;
	cis_shared_data *cis_data = cis->cis_data;
	ktime_t st = ktime_get();

	FIMC_BUG(!cis->cis_data);

	mode = cis_data->sens_config_index_cur;

	next_mode = priv_runtime->mode_groups[SENSOR_IMX825_MODE_DEFAULT];
	if (next_mode == MODE_GROUP_NONE) {
		err("mode group is none");
		return -1;
	}

	if (cis_data->cur_pattern_mode != SENSOR_TEST_PATTERN_MODE_OFF) {
		dbg_sensor(1, "[%d][%s] cur_pattern_mode (%d)", cis->id, __func__, cis_data->cur_pattern_mode);
		return ret;
	}

	IXC_MUTEX_LOCK(cis->ixc_lock);

	if (sensor_imx825_cis_check_aeb(subdev) == 0)
		goto p_out;

	sensor_imx825_cis_check_lownoise(subdev, &next_mode);
	sensor_imx825_cis_check_12bit(subdev, &next_mode);

	cis_data->sens_config_index_pre = cis_data->sens_config_index_cur;
	cis_data->sens_config_index_cur = next_mode;

	if (mode == next_mode || next_mode == MODE_GROUP_NONE)
		goto p_i2c_unlock;

	next_mode_info = cis->sensor_info->mode_infos[next_mode];

	if (next_mode_info->setfile_fcm.regs && next_mode_info->setfile_fcm.size)
		ret |= sensor_cis_write_registers(subdev, next_mode_info->setfile_fcm);
	else
		ret |= sensor_cis_write_registers(subdev, next_mode_info->setfile);

	info("[%d][%s][%d] fcm=[%d]\n",
		cis->id, __func__, cis_data->sen_vsync_count,
		((next_mode_info->setfile_fcm.regs && next_mode_info->setfile_fcm.size) ? 1 : 0));

	if (ret < 0) {
		err("sensor_imx825_set_registers fail!!");
	}

p_out:
	CALL_CISOPS(cis, cis_data_calculation, subdev, next_mode);

	if (!cis_data->video_mode
		&& cis_data->pre_lownoise_mode != IS_CIS_LNOFF
		&& cis_data->cur_lownoise_mode == IS_CIS_LNOFF) {
		cis_data->fll_margin_ratio = 1100; /* x1.1*/
		info("[%d][%s][%d] LN[%d => %d], apply fll margin ratio [%d]\n",
			cis->id, __func__, cis_data->sen_vsync_count,
			cis_data->pre_lownoise_mode, cis_data->cur_lownoise_mode, cis_data->fll_margin_ratio);
	}

	info("[%d][%s][%d] pre(%d, %s)->cur(%d, %s), 12bit[%d] LN[%d] AEB[%d], time %lldus\n",
		cis->id, __func__, cis_data->sen_vsync_count,
		cis_data->sens_config_index_pre,
		cis->sensor_info->mode_infos[cis_data->sens_config_index_pre]->name,
		cis_data->sens_config_index_cur,
		cis->sensor_info->mode_infos[cis_data->sens_config_index_cur]->name,
		cis_data->cur_12bit_mode,
		cis_data->cur_lownoise_mode,
		cis_data->cur_hdr_mode,
		PABLO_KTIME_US_DELTA_NOW(st));

	cis_data->seamless_mode_changed = true;

p_i2c_unlock:
	IXC_MUTEX_UNLOCK(cis->ixc_lock);

	return ret;
}

int sensor_imx825_cis_get_seamless_mode_info(struct v4l2_subdev *subdev)
{
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	struct sensor_imx825_private_runtime *priv_runtime = (struct sensor_imx825_private_runtime *)cis->priv_runtime;
	cis_shared_data *cis_data = cis->cis_data;
	int ret = 0, cnt = 0;
	u32 mode;

	if (priv_runtime->mode_groups[SENSOR_IMX825_MODE_IDCG] != MODE_GROUP_NONE) {
		cis_data->seamless_mode_info[cnt].mode = SENSOR_MODE_REAL_12BIT;
		sensor_cis_get_mode_info(subdev, priv_runtime->mode_groups[SENSOR_IMX825_MODE_IDCG],
			&cis_data->seamless_mode_info[cnt]);
		cnt++;
	}
	if (priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN2] != MODE_GROUP_NONE) {
		cis_data->seamless_mode_info[cnt].mode = SENSOR_MODE_LN2;
		sensor_cis_get_mode_info(subdev, priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN2],
			&cis_data->seamless_mode_info[cnt]);
		cnt++;
	}

	if (priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4] != MODE_GROUP_NONE) {
		cis_data->seamless_mode_info[cnt].mode = SENSOR_MODE_LN4;
		sensor_cis_get_mode_info(subdev, priv_runtime->mode_groups[SENSOR_IMX825_MODE_LN4],
			&cis_data->seamless_mode_info[cnt]);
		cnt++;
	}

	mode = cis->cis_data->sens_config_index_cur;
	if (cis->sensor_info->mode_infos[mode]->aeb_support) {
		cis_data->seamless_mode_info[cnt].mode = SENSOR_MODE_AEB;
		sensor_cis_get_mode_info(subdev, mode, &cis_data->seamless_mode_info[cnt]);
		cnt++;
	}

	cis_data->seamless_mode_cnt = cnt;

	return ret;
}

int sensor_imx825_cis_mode_change(struct v4l2_subdev *subdev, u32 mode)
{
	int ret = 0;
	struct is_device_sensor *device;
	const struct sensor_cis_mode_info *mode_info;
	struct is_cis *cis = sensor_cis_get_cis(subdev);

	device = (struct is_device_sensor *)v4l2_get_subdev_hostdata(subdev);
	WARN_ON(!device);

	if (mode >= cis->sensor_info->mode_count) {
		err("invalid mode(%d)!!", mode);
		return -EINVAL;
	}

	sensor_imx825_cis_set_mode_group(subdev, mode);

	IXC_MUTEX_LOCK(cis->ixc_lock);

	info("[%d][%s] sensor mode(%d)\n", cis->id, __func__, mode);

	mode_info = cis->sensor_info->mode_infos[mode];

	ret |= sensor_cis_write_registers(subdev, mode_info->setfile);
	if (ret < 0) {
		err("sensor_imx825_set_registers fail!!");
		goto p_err_i2c_unlock;
	}

	cis->cis_data->sens_config_index_pre = mode;

	/* AEB disable */
	sensor_imx825_cis_aeb_off(subdev);

	info("[%d][%s] mode[%d, %s] 12bit[%d] LN[%d] AEB[%d]\n",
		cis->id, __func__, mode,
		mode_info->name,
		cis->cis_data->cur_12bit_mode,
		cis->cis_data->cur_lownoise_mode,
		cis->cis_data->cur_hdr_mode);

p_err_i2c_unlock:
	IXC_MUTEX_UNLOCK(cis->ixc_lock);
	sensor_imx825_cis_update_seamless_mode(subdev);
	sensor_imx825_cis_get_seamless_mode_info(subdev);

	/* sensor_imx825_cis_log_status(subdev); */
	info("[%d][%s] X\n", cis->id, __func__);

	return ret;
}

int sensor_imx825_cis_set_global_setting(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	struct sensor_imx825_private_data *priv = (struct sensor_imx825_private_data *)cis->sensor_info->priv;
	struct is_device_sensor *device;
	device = (struct is_device_sensor *)v4l2_get_subdev_hostdata(subdev);
	WARN_ON(!device);

	IXC_MUTEX_LOCK(cis->ixc_lock);
	info("[%d][%s] global setting start\n", cis->id, __func__);
	ret |= sensor_cis_write_registers(subdev, priv->global);

	if (ret < 0) {
		err("sensor_imx825_set_registers fail!!");
		goto p_err;
	}

	info("[%d][%s] global setting done\n", cis->id, __func__);

p_err:
	IXC_MUTEX_UNLOCK(cis->ixc_lock);

	return ret;
}

int sensor_imx825_cis_stream_on(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_device_sensor *device;
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	ktime_t st = ktime_get();
	struct sensor_imx825_private_runtime *priv_runtime = (struct sensor_imx825_private_runtime *)cis->priv_runtime;
	const struct sensor_cis_mode_info *mode_info;

	device = (struct is_device_sensor *)v4l2_get_subdev_hostdata(subdev);
	WARN_ON(!device);

	dbg_sensor(1, "[MOD:D:%d] %s\n", cis->id, __func__);

	mode_info = cis->sensor_info->mode_infos[cis->cis_data->sens_config_index_pre];

	is_vendor_set_mipi_clock(device);

	IXC_MUTEX_LOCK(cis->ixc_lock);

	/* EMB OFF */
	cis->ixc_ops->write8(cis->client, 0x3970, 0x00);

	/* init_aeb */
	if (is_sensor_check_aeb_mode(device)) {
		sensor_imx825_cis_init_aeb(cis, mode_info->aeb_support);
	}

	/* Sensor stream on */
	info("[%d][%s] vsync_count(%d)\n", cis->id, __func__, cis->cis_data->sen_vsync_count);
	cis->ixc_ops->write8(cis->client, 0x0100, 0x01);

	IXC_MUTEX_UNLOCK(cis->ixc_lock);

	cis->cis_data->stream_on = true;

	priv_runtime->lte_on_delay = 0;

	if (IS_ENABLED(DEBUG_SENSOR_TIME))
		dbg_sensor(1, "[%s] time %lldus", __func__, PABLO_KTIME_US_DELTA_NOW(st));

	return ret;
}

int sensor_imx825_cis_stream_off(struct v4l2_subdev *subdev)
{
	int ret = 0;
	u8 cur_frame_count = 0;
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	ktime_t st = ktime_get();

	dbg_sensor(1, "[MOD:D:%d] %s\n", cis->id, __func__);

	IXC_MUTEX_LOCK(cis->ixc_lock);
	if (cis->cis_data->cur_hdr_mode == SENSOR_HDR_MODE_2AEB_2VC
		&& cis->cis_data->pre_hdr_mode == SENSOR_HDR_MODE_2AEB_2VC) {
		info("[%d][%s] current AEB status is enabled. need AEB disable\n", cis->id, __func__);
		cis->cis_data->pre_hdr_mode = SENSOR_HDR_MODE_SINGLE;
		cis->cis_data->cur_hdr_mode = SENSOR_HDR_MODE_SINGLE;

		/* AEB disable */
		sensor_imx825_cis_aeb_off(subdev);
	}

	cis->ixc_ops->read8(cis->client, 0x0005, &cur_frame_count);
	info("[%d][%s] frame_count(0x%x) vsync_count(%d)\n", cis->id, __func__, cur_frame_count, cis->cis_data->sen_vsync_count);

	cis->ixc_ops->write8(cis->client, 0x0100, 0x00);
	IXC_MUTEX_UNLOCK(cis->ixc_lock);

	cis->cis_data->stream_on = false;

	if (IS_ENABLED(DEBUG_SENSOR_TIME))
		dbg_sensor(1, "[%s] time %lldus", __func__, PABLO_KTIME_US_DELTA_NOW(st));

	return ret;
}

void sensor_imx825_cis_data_calculation(struct v4l2_subdev *subdev, u32 mode)
{
//	u32 frame_valid_us = 0;
	struct is_cis *cis;
	cis_shared_data *cis_data;
	const struct sensor_cis_mode_info *mode_info;
	u64 pclk_hz;
//	u64 valid_height;
	u16 llp;

	WARN_ON(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);
	WARN_ON(!cis);

	if (mode >= cis->sensor_info->mode_count) {
		err("invalid mode(%d)!", mode);
		return;
	}

	WARN_ON(!cis->cis_data);

	cis_data = cis->cis_data;
	mode_info = cis->sensor_info->mode_infos[mode];

	pclk_hz = mode_info->pclk;
	llp = mode_info->line_length_pck;

	sensor_cis_data_calculation(subdev, mode);
}

int sensor_imx825_cis_set_test_pattern(struct v4l2_subdev *subdev, camera2_sensor_ctl_t *sensor_ctl)
{
	int ret = 0;
	struct is_cis *cis = sensor_cis_get_cis(subdev);

	dbg_sensor(1, "[MOD:D:%d] %s, cur_pattern_mode(%d), testPatternMode(%d)\n", cis->id, __func__,
		cis->cis_data->cur_pattern_mode, sensor_ctl->testPatternMode);

	if (cis->cis_data->cur_pattern_mode != sensor_ctl->testPatternMode) {
		if (sensor_ctl->testPatternMode == SENSOR_TEST_PATTERN_MODE_OFF) {
			info("[%d][%s] set DEFAULT pattern! (mode : %d)\n", cis->id, __func__, sensor_ctl->testPatternMode);

			IXC_MUTEX_LOCK(cis->ixc_lock);
			cis->ixc_ops->write16(cis->client, 0x0600, 0x0000);
			cis->ixc_ops->write8(cis->client, 0x3800, 0x00);
			IXC_MUTEX_UNLOCK(cis->ixc_lock);

			cis->cis_data->cur_pattern_mode = sensor_ctl->testPatternMode;
		} else if (sensor_ctl->testPatternMode == SENSOR_TEST_PATTERN_MODE_BLACK) {
			info("[%d][%s] set BLACK pattern! (mode :%d), Data : 0x(%x, %x, %x, %x)\n",
				cis->id, __func__, sensor_ctl->testPatternMode,
				(unsigned short)sensor_ctl->testPatternData[0],
				(unsigned short)sensor_ctl->testPatternData[1],
				(unsigned short)sensor_ctl->testPatternData[2],
				(unsigned short)sensor_ctl->testPatternData[3]);

			IXC_MUTEX_LOCK(cis->ixc_lock);
			cis->ixc_ops->write16(cis->client, 0x0600, 0x0001);
			cis->ixc_ops->write8(cis->client, 0x3800, 0x20);
			IXC_MUTEX_UNLOCK(cis->ixc_lock);

			cis->cis_data->cur_pattern_mode = sensor_ctl->testPatternMode;
		}
	}

	return ret;
}

int sensor_imx825_cis_adjust_cit_for_extremely_br(struct v4l2_subdev *subdev, u32 *cit)
{
	struct is_cis *cis = sensor_cis_get_cis(subdev);
	int input_cit = *cit;

	if (cis->cis_data->min_coarse_integration_time == 7) {
		if (input_cit <= 15)
			*cit = ALIGN_DOWN(input_cit, 2) + 1;
	} else if (cis->cis_data->min_coarse_integration_time == 6) {
		if  (input_cit <= 14)
			*cit = ALIGN_DOWN(input_cit, 4) + 2;
		else
			*cit = ALIGN_DOWN(input_cit, 2);
	}

	if (*cit != input_cit)
		dbg_sensor(1, "[MOD:D:%d] %s : [%d] => [%d]\n", cis->id, __func__, input_cit, *cit);

	return 0;
}

static struct is_cis_ops cis_ops_imx825 = {
	.cis_init = sensor_imx825_cis_init,
	.cis_deinit = sensor_imx825_cis_deinit,
	.cis_log_status = sensor_imx825_cis_log_status,
	.cis_init_state = sensor_cis_init_state,
	.cis_group_param_hold = sensor_cis_set_group_param_hold,
	.cis_set_global_setting = sensor_imx825_cis_set_global_setting,
	.cis_mode_change = sensor_imx825_cis_mode_change,
	.cis_stream_on = sensor_imx825_cis_stream_on,
	.cis_stream_off = sensor_imx825_cis_stream_off,
	.cis_data_calculation = sensor_imx825_cis_data_calculation,
	.cis_set_exposure_time = sensor_cis_set_exposure_time,
	.cis_get_min_exposure_time = sensor_cis_get_min_exposure_time,
	.cis_get_max_exposure_time = sensor_cis_get_max_exposure_time,
	.cis_adjust_frame_duration = sensor_cis_adjust_frame_duration,
	.cis_set_frame_duration = sensor_cis_set_frame_duration,
	.cis_set_frame_rate = sensor_cis_set_frame_rate,
	.cis_adjust_analog_gain = sensor_cis_adjust_analog_gain,
	.cis_set_analog_gain = sensor_cis_set_analog_gain,
	.cis_get_analog_gain = sensor_cis_get_analog_gain,
	.cis_get_min_analog_gain = sensor_cis_get_min_analog_gain,
	.cis_get_max_analog_gain = sensor_cis_get_max_analog_gain,
	.cis_calc_again_code = sensor_imx825_cis_calc_again_code,
	.cis_calc_again_permile = sensor_imx825_cis_calc_again_permile,
	.cis_set_digital_gain = sensor_cis_set_digital_gain,
	.cis_get_digital_gain = sensor_cis_get_digital_gain,
	.cis_get_min_digital_gain = sensor_cis_get_min_digital_gain,
	.cis_get_max_digital_gain = sensor_cis_get_max_digital_gain,
	.cis_calc_dgain_code = sensor_cis_calc_dgain_code,
	.cis_calc_dgain_permile = sensor_cis_calc_dgain_permile,
	.cis_compensate_gain_for_extremely_br = sensor_cis_compensate_gain_for_extremely_br,
	.cis_adjust_cit_for_extremely_br = sensor_imx825_cis_adjust_cit_for_extremely_br,
	.cis_wait_streamoff = sensor_cis_wait_streamoff,
	.cis_wait_streamon = sensor_cis_wait_streamon,
	.cis_check_rev_on_init = sensor_cis_check_rev_on_init,
	.cis_set_initial_exposure = sensor_cis_set_initial_exposure,
//	.cis_recover_stream_on = sensor_cis_recover_stream_on,
//	.cis_recover_stream_off = sensor_cis_recover_stream_off,
	.cis_set_test_pattern = sensor_imx825_cis_set_test_pattern,
	.cis_update_seamless_state = sensor_cis_update_seamless_state,
	.cis_seamless_ctl_before_stream = sensor_cis_seamless_ctl_before_stream,
	.cis_wait_seamless_update_delay = sensor_cis_wait_seamless_update_delay,
	.cis_update_seamless_mode = sensor_imx825_cis_update_seamless_mode,
	.cis_set_flip_mode = sensor_cis_set_flip_mode,
};

static int cis_imx825_probe_i2c(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret;
	u32 mclk_freq_khz;
	struct is_cis *cis;
	struct is_device_sensor_peri *sensor_peri;
	char const *setfile;
	struct device_node *dnode = client->dev.of_node;
	struct sensor_imx825_private_runtime *priv_runtime;
	struct device *dev;

	ret = sensor_cis_probe(client, &(client->dev), &sensor_peri, I2C_TYPE);
	if (ret) {
		probe_info("%s: sensor_cis_probe ret(%d)\n", __func__, ret);
		return ret;
	}

	dev = &client->dev;
	cis = &sensor_peri->cis;
	cis->ctrl_delay = N_PLUS_TWO_FRAME;
	cis->cis_ops = &cis_ops_imx825;
	/* belows are depend on sensor cis. MUST check sensor spec */
	cis->bayer_order = OTF_INPUT_ORDER_BAYER_BG_GR;
	cis->reg_addr = &sensor_imx825_reg_addr;
	cis->priv_runtime =
			devm_kzalloc(dev, sizeof(struct sensor_imx825_private_runtime), GFP_KERNEL);
	if (!cis->priv_runtime) {
		devm_kfree(dev, cis->cis_data);
		devm_kfree(dev, cis->subdev);
		probe_err("cis->priv_runtime is NULL");
		return -ENOMEM;
	}

	ret = of_property_read_string(dnode, "setfile", &setfile);
	if (ret) {
		err("setfile index read fail(%d), take default setfile!!", ret);
		setfile = "default";
	}

	priv_runtime = (struct sensor_imx825_private_runtime *)cis->priv_runtime;
	priv_runtime->lte_on_delay = 0;
	priv_runtime->is_front = 0;

	mclk_freq_khz = sensor_peri->module->pdata->mclk_freq_khz;

	if (mclk_freq_khz == 19200)	{
		if (strcmp(setfile, "setA") == 0) {
			probe_info("%s setfile_A mclk: 19.2MHz\n", __func__);
			cis->sensor_info = &sensor_imx825_info_A_19p2;
			priv_runtime->is_front = true;
		} else {
			err("%s setfile index out of bound, take default (setfile_A mclk: 19.2MHz)", __func__);
			cis->sensor_info = &sensor_imx825_info_A_19p2;
		}
	}

	is_vendor_set_mipi_mode(cis);

	probe_info("%s done\n", __func__);

	return ret;
}

static const struct of_device_id sensor_cis_imx825_match[] = {
	{
		.compatible = "samsung,exynos-is-cis-imx825",
	},
	{},
};
MODULE_DEVICE_TABLE(of, sensor_cis_imx825_match);

static const struct i2c_device_id sensor_cis_imx825_idt[] = {
	{ SENSOR_NAME, 0 },
	{},
};

static struct i2c_driver sensor_cis_imx825_driver = {
	.probe	= cis_imx825_probe_i2c,
	.driver = {
		.name	= SENSOR_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = sensor_cis_imx825_match,
		.suppress_bind_attrs = true,
	},
	.id_table = sensor_cis_imx825_idt
};

#ifdef MODULE
module_driver(sensor_cis_imx825_driver, i2c_add_driver,
	i2c_del_driver)
#else
static int __init sensor_cis_imx825_init(void)
{
	int ret;

	ret = i2c_add_driver(&sensor_cis_imx825_driver);
	if (ret)
		err("failed to add %s driver: %d\n",
			sensor_cis_imx825_driver.driver.name, ret);

	return ret;
}
late_initcall_sync(sensor_cis_imx825_init);
#endif

MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: fimc-is");
