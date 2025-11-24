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

#include <linux/version.h>
#include <soc/samsung/exynos_pm_qos.h>
#include <linux/pm_opp.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <soc/samsung/exynos-devfreq.h>
#if IS_ENABLED(CONFIG_SOC_S5E9945)
#include <dt-bindings/soc/samsung/s5e9945-devfreq.h>
#else
#include <dt-bindings/soc/samsung/s5e8845-devfreq.h>
#endif

#include "npu-device.h"
#include "npu-state.h"
#include "npu-util-common.h"

struct npu_state npu_state;

static ssize_t sysfs_show_cur_state(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int i = 0;

	i += scnprintf(buf + i, PAGE_SIZE - i, "%u\n", npu_state.cur_state);

	return i;
}

static ssize_t sysfs_store_cur_state(struct class *class,
		struct class_attribute *attr, const char *buf, size_t count)
{
	int ret = 0, value = 0;

	ret = sscanf(buf, "%d", &value);
	if (ret != 1) {
		pr_err("%s(%d) Failed to parse input\n", __func__, __LINE__);
		return count;
	}

	if (value < 0 || value >= npu_state.node_num) {
		pr_err("%s(%d) Invalid state value\n", __func__, __LINE__);
		return count;
	}

	npu_state.cur_state = (u32)value;
	npu_dvfs_pm_qos_update_request(&npu_state.npu_state_dnc_max,
			npu_state.npu_freq_level[npu_state.cur_state].dnc);
	npu_dvfs_pm_qos_update_request(&npu_state.npu_state_npu0_max,
			npu_state.npu_freq_level[npu_state.cur_state].npu0);
#if IS_ENABLED(CONFIG_SOC_S5E9945)
	npu_dvfs_pm_qos_update_request(&npu_state.npu_state_npu1_max,
			npu_state.npu_freq_level[npu_state.cur_state].npu1);
	npu_dvfs_pm_qos_update_request(&npu_state.npu_state_dsp_max,
			npu_state.npu_freq_level[npu_state.cur_state].dsp);
#endif

	return count;
}

static ssize_t sysfs_show_state_list(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int i = 0;
	int count = 0;


	for(i = 0; i < npu_state.node_num; i++) {
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%d ", i);
	}
	count += scnprintf(&buf[count], PAGE_SIZE - count, "\n");

	return count;
}

static ssize_t sysfs_show_npu_clock(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int i = 0;
	int count = 0;
	unsigned long dnc_freq = 0;
	unsigned long npu0_freq = 0;
	unsigned long npu1_freq = 0;
	unsigned long dsp_freq = 0;


	for(i = 0; i < npu_state.node_count; i++) {
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%s, ", npu_state.node_name[i]);
	}
	count += scnprintf(&buf[count], PAGE_SIZE - count, "\n");

	dnc_freq = exynos_devfreq_get_domain_freq(DEVFREQ_DNC);
#if IS_ENABLED(CONFIG_SOC_S5E9945)
	npu0_freq = exynos_devfreq_get_domain_freq(DEVFREQ_NPU0);
	npu1_freq = exynos_devfreq_get_domain_freq(DEVFREQ_NPU1);
	dsp_freq = exynos_devfreq_get_domain_freq(DEVFREQ_DSP);
	count += scnprintf(buf + count, PAGE_SIZE - count, "%lu, %lu, %lu, %lu\n",
			dnc_freq, npu0_freq, npu1_freq, dsp_freq);
#else
	npu0_freq = exynos_devfreq_get_domain_freq(DEVFREQ_NPU);
	(void)npu1_freq;
	(void)dsp_freq;
	count += scnprintf(buf + count, PAGE_SIZE - count, "%lu, %lu\n", dnc_freq, npu0_freq);
#endif

	return count;
}

static ssize_t sysfs_show_npu_clock_table(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int i = 0;
	int count = 0;

	for(i = 0; i < npu_state.node_count; i++) {
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%s, ", npu_state.node_name[i]);
	}
	count += scnprintf(&buf[count], PAGE_SIZE - count, "\n");

	for(i = 0; i < npu_state.node_num; i++) {
#if IS_ENABLED(CONFIG_SOC_S5E9945)
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%u, %u, %u, %u\n",
				npu_state.npu_freq_level[i].dnc,
				npu_state.npu_freq_level[i].npu0,
				npu_state.npu_freq_level[i].npu1,
				npu_state.npu_freq_level[i].dsp);
#else
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%u, %u\n",
		npu_state.npu_freq_level[i].dnc, npu_state.npu_freq_level[i].npu0);
#endif
	}

	return count;
}

int npu_state_get_freq_level(unsigned int freq)
{
    int i = 0;

    for (i = 0; i < npu_state.node_num; i++) {
        if (freq == npu_state.npu_freq_level[i].npu0) {
            return i;
        }
    }

    npu_warn("Frequency %u is not matched with any level\n", freq);
    return npu_state.node_num - 1;
}

void npu_state_set_start_time(struct npu_session *session)
{
	if (!session)
		return;
	session->start_time = npu_get_time_us();
}

int npu_state_set_execution_time(struct npu_queue *queue)
{
	struct npu_session *session;
	struct npu_vertex_ctx *vctx;
	u32 execution_time = 0;
	u64 now = 0;

	if (!queue)
		return 0;

	vctx = container_of(queue, struct npu_vertex_ctx, queue);
	session = container_of(vctx, struct npu_session, vctx);
	now = npu_get_time_us();

	if (now < session->start_time)
		return 0;

	execution_time = (u32)(now - session->start_time);
	return npu_state_update_time_in_state(execution_time);
}

int npu_state_update_time_in_state(u32 execution_time)
{
	unsigned long npu0_freq = 0;
	u32 freq_level = 0;
#if IS_ENABLED(CONFIG_SOC_S5E9945)
	npu0_freq = exynos_devfreq_get_domain_freq(DEVFREQ_NPU0);
#else
	npu0_freq = exynos_devfreq_get_domain_freq(DEVFREQ_NPU);
#endif

	freq_level = npu_state_get_freq_level(npu0_freq);
	npu_state.time_in_state[freq_level] += execution_time / 1000;
	return 0;
}

static ssize_t sysfs_show_time_in_state(struct class *class,
	struct class_attribute *attr, char *buf)
{
	int i = 0;
	int count = 0;

	for(i = 0; i < npu_state.node_num; i++) {
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%u\t%u\n",
				npu_state.npu_freq_level[i].npu0,
				npu_state.time_in_state[i]);
	}

	return count;
}

static int npu_qos_parsing_dt(struct npu_device *device)
{
	int i = 0, ret = 0;
	struct device *dev = device->dev;

	ret = of_property_read_u32(dev->of_node, "samsung,npunode-num",
				&npu_state.node_num);
	if (ret) {
		probe_err("failed parsing npunode dt(npunode-num)\n");
		return ret;
	}
	if (npu_state.node_num >= NPU_MAX_FREQ_NODE_NUM) {
		probe_err("npunode-num(%d) is bigger than NPU_MAX_NODE_NUM(%d)\n",
			npu_state.node_num, NPU_MAX_FREQ_NODE_NUM);
		return -EINVAL;
	}

	npu_state.npu_freq_level = (struct npu_state_freq_info*)kmalloc(
		sizeof(struct npu_state_freq_info) * npu_state.node_num, GFP_KERNEL);
	if (!npu_state.npu_freq_level) {
		probe_err("Failed to allocate memory for npu_freq_level\n");
		return -ENOMEM;
	}

	npu_state.time_in_state = (u32*)kmalloc(
		sizeof(u32) * npu_state.node_num, GFP_KERNEL);
	if (!npu_state.time_in_state) {
		probe_err("Failed to allocate memory for time_in_state\n");
		kfree(npu_state.npu_freq_level);
		npu_state.npu_freq_level = NULL;
		return -ENOMEM;
	}
	for (int i = 0; i < npu_state.node_num; i++) {
		npu_state.time_in_state[i] = 0;
	}

	npu_state.node_count = of_property_count_strings(dev->of_node, "samsung,npunode-names");
	if (IS_ERR_VALUE((unsigned long)npu_state.node_count)) {
		probe_err("failed to count node name in %s node\n", dev->of_node->name);
		ret = -EINVAL;
		goto p_err;
	}

	/* npu - dnc engine */
	ret = of_property_read_u32_array(dev->of_node, "samsung,npunode-freq",
			(u32 *)npu_state.npu_freq_level, npu_state.node_count * npu_state.node_num);
	if (ret) {
		probe_err("failed parsing governor dt(npugovernor-npufreq)\n");
		goto p_err;
	}
	for(i = 0; i < npu_state.node_count; i++) {
		ret = of_property_read_string_index(dev->of_node, "samsung,npunode-names",
					i, (const char **)&npu_state.node_name[i]);
		if (ret) {
			probe_err("failed to read node name %d from %s node : %d\n",
					i, dev->of_node->name, ret);
				goto p_err;
		}
	}
#if IS_ENABLED(CONFIG_SOC_S5E9945)
	probe_info("NPU (DNC NPU0 NPU1 DSP) freq\n");
	for (i = 0; i < npu_state.node_num; i++)
		probe_info("[%d] %u %u %u %u\n", i,
				npu_state.npu_freq_level[i].dnc,
				npu_state.npu_freq_level[i].npu0,
				npu_state.npu_freq_level[i].npu1,
				npu_state.npu_freq_level[i].dsp);
#else
	probe_info("NPU (DNC NPU) freq\n");
	for (i = 0; i < npu_state.node_num; i++)
		probe_info("[%d] %u %u\n", i,
				npu_state.npu_freq_level[i].dnc,
				npu_state.npu_freq_level[i].npu0);
#endif
	return 0;
p_err:
	kfree(npu_state.npu_freq_level);
	npu_state.npu_freq_level = NULL;
	kfree(npu_state.time_in_state);
	npu_state.time_in_state = NULL;
	return ret;
}

static struct class_attribute npu_state_sysfs_attr[] = {
	__ATTR(cur_state, 0600,
		sysfs_show_cur_state,
		sysfs_store_cur_state),
	__ATTR(state_list, 0400,
		sysfs_show_state_list,
		NULL),
	__ATTR(npu_clock, 0444,
		sysfs_show_npu_clock,
		NULL),
	__ATTR(npu_clock_table, 0400,
		sysfs_show_npu_clock_table,
		NULL),
	__ATTR(time_in_state, 0400,
		sysfs_show_time_in_state,
		NULL),
};

static struct attribute *npu_state_class_attrs[] = {
	&npu_state_sysfs_attr[0].attr,
	&npu_state_sysfs_attr[1].attr,
	&npu_state_sysfs_attr[2].attr,
	&npu_state_sysfs_attr[3].attr,
	&npu_state_sysfs_attr[4].attr,
	NULL,
};
ATTRIBUTE_GROUPS(npu_state_class);

static struct class npu_state_class = {
	.name		= "npu_state",
	.class_groups	= npu_state_class_groups,
};

int npu_state_sysfs_create(struct npu_system *system)
{
	int ret = 0;
	struct npu_device *device;

	device = container_of(system, struct npu_device, system);

	ret = npu_qos_parsing_dt(device);
	if (ret) {
		probe_err("npu_qos parsing dt failed\n");
		return ret;
	}

#if IS_ENABLED(CONFIG_SOC_S5E9945)
	exynos_pm_qos_add_request(&npu_state.npu_state_npu0_max,
			PM_QOS_NPU0_THROUGHPUT_MAX,
			PM_QOS_NPU0_THROUGHPUT_MAX_DEFAULT_VALUE);
	exynos_pm_qos_add_request(&npu_state.npu_state_npu1_max,
			PM_QOS_NPU1_THROUGHPUT_MAX,
			PM_QOS_NPU1_THROUGHPUT_MAX_DEFAULT_VALUE);
	exynos_pm_qos_add_request(&npu_state.npu_state_dsp_max,
			PM_QOS_DSP_THROUGHPUT_MAX,
			PM_QOS_DSP_THROUGHPUT_MAX_DEFAULT_VALUE);
#else
	exynos_pm_qos_add_request(&npu_state.npu_state_npu0_max,
			PM_QOS_NPU_THROUGHPUT_MAX,
			PM_QOS_NPU_THROUGHPUT_MAX_DEFAULT_VALUE);
#endif
	exynos_pm_qos_add_request(&npu_state.npu_state_dnc_max,
			PM_QOS_DNC_THROUGHPUT_MAX,
			PM_QOS_DNC_THROUGHPUT_MAX_DEFAULT_VALUE);
	npu_state.cur_state = 0;

	probe_info("npu_state create\n");
	probe_info("creating sysfs group %s\n", npu_state_class.name);

	ret = class_register(&npu_state_class);
	if (ret) {
		probe_err("failed(%d) to create sysfs for %s\n",
						ret, npu_state_class.name);
	}

	return ret;
}

