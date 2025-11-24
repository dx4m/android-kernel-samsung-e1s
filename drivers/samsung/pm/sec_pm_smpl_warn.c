/* drivers/samsung/pm/sec_pm_smpl_warn.c
 *
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 * Author: Minsung Kim <ms925.kim@samsung.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>
#include <linux/interrupt.h>
#include <linux/sec_class.h>
#include <linux/sec_pm_smpl_warn.h>
#include <soc/samsung/freq-qos-tracer.h>
#include <soc/samsung/exynos_pm_qos.h>

#define BUF_SIZE	128
#define PIN_HIGH	1
#define PIN_LOW		0

struct cpufreq_dev {
	struct cpufreq_frequency_table *freq_table;
	unsigned int max_freq;
	unsigned int max_level;
	unsigned int cur_level;
	struct cpufreq_policy *policy;
	struct list_head node;
	struct freq_qos_request qos_req;
	char qos_name[32];
};

struct sec_pm_smpl_warn_info {
	struct device			*dev;
	struct device			*sec_dev;
	int				num_policy_cpus;
	u32				*policy_cpus;
	int				smpl_warn_gpio;
	int				ocp_warn_gpio;
	int				ocp_warn_irq;
	unsigned int			ocp_warn_cnt;
	ktime_t				ocp_warn_start_time;
	s64				ocp_warn_accumulated_time;
};

static DEFINE_MUTEX(cpufreq_dev_list_lock);
static LIST_HEAD(cpufreq_dev_list);

static unsigned int *gpu_freq_table;
static unsigned int gpu_freq_table_size;
static struct exynos_pm_qos_request sec_pm_gpu_max_qos;

static unsigned long throttle_count;
static bool smpl_warn_init_done;

static int sec_pm_smpl_warn_set_max_freq(struct cpufreq_dev *cdev,
				 unsigned int level)
{
	if (WARN_ON(level > cdev->max_level))
		return -EINVAL;

	if (cdev->cur_level == level)
		return 0;

	cdev->max_freq = cdev->freq_table[level].frequency;
	cdev->cur_level = level;

	pr_info("%s: throttle cpu%d : %u KHz\n", __func__,
			cdev->policy->cpu, cdev->max_freq);

	return freq_qos_update_request(&cdev->qos_req,
				cdev->freq_table[level].frequency);
}

static unsigned long get_level(struct cpufreq_dev *cdev,
			       unsigned long freq)
{
	struct cpufreq_frequency_table *freq_table = cdev->freq_table;
	unsigned long level;

	for (level = 1; level <= cdev->max_level; level++)
		if (freq >= freq_table[level].frequency)
			break;

	return level;
}

static void sec_pm_throttle_gpu_freq(unsigned long count)
{
	unsigned long freq;
	unsigned int idx = gpu_freq_table_size / 2;

	idx += count;

	if (idx >= gpu_freq_table_size)
		return;

	freq = gpu_freq_table[idx];

	pr_info("%s: %lu\n", __func__, freq);

	if (sec_pm_gpu_max_qos.exynos_pm_qos_class) {
		if (exynos_pm_qos_request_active(&sec_pm_gpu_max_qos))
			exynos_pm_qos_update_request(&sec_pm_gpu_max_qos, freq);
	}
}

static void sec_pm_unthrottle_gpu_freq(void)
{
	unsigned long freq = PM_QOS_GPU_FREQ_MAX_DEFAULT_VALUE;

	pr_info("%s\n", __func__);

	if (sec_pm_gpu_max_qos.exynos_pm_qos_class) {
		if (exynos_pm_qos_request_active(&sec_pm_gpu_max_qos))
			exynos_pm_qos_update_request(&sec_pm_gpu_max_qos, freq);
	}
}

/* This function should be called during SMPL_WARN interrupt is active */
int sec_pm_smpl_warn_throttle_by_one_step(void)
{
	struct cpufreq_dev *cdev;
	unsigned long level;
	unsigned long freq;

	if (unlikely(!smpl_warn_init_done)) {
		pr_warn("%s: Not initialized\n", __func__);
		return -EPERM;
	}

	++throttle_count;

	list_for_each_entry(cdev, &cpufreq_dev_list, node) {
		if (!cdev->policy || !cdev->freq_table) {
			pr_warn("%s: No cdev\n", __func__);
			continue;
		}

		/* Skip LITTLE cluster */
		if (!cdev->policy->cpu)
			continue;

		freq = cdev->freq_table[0].frequency / 2;
		level = get_level(cdev, freq);
		level += throttle_count;

		if (level > cdev->max_level)
			level = cdev->max_level;

		sec_pm_smpl_warn_set_max_freq(cdev, level);
	}

	/* Throttle GPU frequency */
	sec_pm_throttle_gpu_freq(throttle_count);

	return throttle_count;
}
EXPORT_SYMBOL_GPL(sec_pm_smpl_warn_throttle_by_one_step);

void sec_pm_smpl_warn_unthrottle(void)
{
	struct cpufreq_dev *cdev;

	if (unlikely(!smpl_warn_init_done)) {
		pr_warn("%s: Not initialized\n", __func__);
		return;
	}

	pr_info("%s: throttle_count: %lu\n", __func__, throttle_count);

	if (!throttle_count)
		return;

	throttle_count = 0;

	list_for_each_entry(cdev, &cpufreq_dev_list, node)
		sec_pm_smpl_warn_set_max_freq(cdev, 0);

	sec_pm_unthrottle_gpu_freq();
}
EXPORT_SYMBOL_GPL(sec_pm_smpl_warn_unthrottle);

static unsigned int find_next_max_cpufreq(struct cpufreq_frequency_table *table,
				  unsigned int prev_max)
{
	struct cpufreq_frequency_table *pos;
	unsigned int max = 0;

	cpufreq_for_each_valid_entry(pos, table) {
		if (pos->frequency > max && pos->frequency < prev_max)
			max = pos->frequency;
	}

	return max;
}

static ssize_t gpu_freq_max_pm_qos_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	int val;

	val = exynos_pm_qos_read_req_value(sec_pm_gpu_max_qos.exynos_pm_qos_class,
			&sec_pm_gpu_max_qos);

	if (val < 0) {
		pr_err("%s: failed to read requested value\n", __func__);
		return count;
	}
	count += snprintf(buf, PAGE_SIZE, "%d\n", val);

	return count;
}

static ssize_t gpu_freq_max_pm_qos_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	u32 qos_value;

	if (kstrtou32(buf, 0, &qos_value))
		return -EINVAL;

	if (sec_pm_gpu_max_qos.exynos_pm_qos_class) {
		if (exynos_pm_qos_request_active(&sec_pm_gpu_max_qos))
			exynos_pm_qos_update_request(&sec_pm_gpu_max_qos, qos_value);
	}

	return count;
}
static DEVICE_ATTR_RW(gpu_freq_max_pm_qos);

static ssize_t gpu_freq_table_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	unsigned int i;

	for (i = 0; i < gpu_freq_table_size; i++) {
		count += scnprintf(&buf[count], (PAGE_SIZE - count - 2),
				"%u\n", gpu_freq_table[i]);
	}

	return count;
}
static DEVICE_ATTR_RO(gpu_freq_table);

static ssize_t smpl_warn_val_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sec_pm_smpl_warn_info *info = dev_get_drvdata(dev);
	ssize_t ret = 0;
	int state = gpio_get_value(info->smpl_warn_gpio);

	if (info->smpl_warn_gpio < 0) {
		dev_info(info->dev, "%s: No smpl_warn pin\n", __func__);
		return ret;
	}

	/* smpl warn: active low */
	dev_info(info->dev, "smpl_warn_val: %d\n", state);

	ret = snprintf(buf, sizeof(int), "%d\n", !!state);

	return ret;
}
static DEVICE_ATTR_RO(smpl_warn_val);

static ssize_t ocp_warn_cnt_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sec_pm_smpl_warn_info *info = dev_get_drvdata(dev);
	ssize_t ret = 0;

	ret = snprintf(buf, BUF_SIZE,
			"\"TYPE\":\"0\",\"COUNT\":\"%u\",\"TIME\":\"%lld\",\"LEVEL\":\"0\"",
			info->ocp_warn_cnt, info->ocp_warn_accumulated_time);

	/* reset after read */
	info->ocp_warn_cnt = 0;
	info->ocp_warn_accumulated_time = 0;

	return ret;
}
static DEVICE_ATTR_RO(ocp_warn_cnt);

static struct attribute *sec_pm_smpl_warn_sysfs_attrs[] = {
	&dev_attr_gpu_freq_max_pm_qos.attr,
	&dev_attr_gpu_freq_table.attr,
	&dev_attr_smpl_warn_val.attr,
	&dev_attr_ocp_warn_cnt.attr,
	NULL
};
static const struct attribute_group sec_pm_smpl_warn_sysfs_attr_group = {
	.attrs = sec_pm_smpl_warn_sysfs_attrs,
};

static irqreturn_t sec_pm_ocp_warn_irq_thread(int irq, void *data)
{
	struct sec_pm_smpl_warn_info *info = data;
	int state = gpio_get_value(info->ocp_warn_gpio);

	if (state == PIN_HIGH) {
		info->ocp_warn_cnt++;
		if (info->ocp_warn_start_time == 0)
			info->ocp_warn_start_time = ktime_get();

		dev_info(info->dev, "OCP_WARN state: %d, cnt: %u\n", state,
				info->ocp_warn_cnt);
	} else if (state == PIN_LOW) {
		if (info->ocp_warn_start_time) {
			ktime_t current_time = ktime_get();
			info->ocp_warn_accumulated_time +=
				ktime_ms_delta(current_time, info->ocp_warn_start_time);
			info->ocp_warn_start_time = 0;
		}
		dev_info(info->dev, "OCP_WARN state: %d, accu(%lld ms)\n",
				state, info->ocp_warn_accumulated_time);
	}

	return IRQ_HANDLED;
}

static unsigned int find_next_max_value(unsigned int array[],
		unsigned int array_size, unsigned int prev_max)
{
	unsigned int i, max = 0;

	for (i = 0; i < array_size; i++) {
		if (array[i] > max && array[i] < prev_max)
			max = array[i];
	}

	return max;
}

static int fill_gpu_freq_table_in_descending_order(struct sec_pm_smpl_warn_info *info)
{
	unsigned int i, freq = -1;
	unsigned int *freq_table;

	freq_table = kcalloc(gpu_freq_table_size, sizeof(*freq_table),
			GFP_KERNEL);
	if (!freq_table)
		return -ENOMEM;

	memcpy(freq_table, gpu_freq_table,
			sizeof(*freq_table) * gpu_freq_table_size);

	for (i = 0; i < gpu_freq_table_size; i++) {
		freq = find_next_max_value(freq_table,
				gpu_freq_table_size, freq);
		gpu_freq_table[i] = freq;

		if (!freq)
			dev_warn(info->dev, "%s: table has duplicate entries\n",
					__func__);
	}

	kfree(freq_table);
	return 0;
}

static int sec_pm_smpl_warn_parse_dt(struct sec_pm_smpl_warn_info *info)
{
	struct device_node *dn;
	int ret;
	u32 sgpu_phandle;
	struct device_node *sgpu_node;

	if (!info)
		return -ENODEV;

	dn = info->dev->of_node;

	info->smpl_warn_gpio = of_get_named_gpio(dn, "pmic-smpl-warn-gpio", 0);
	if (info->smpl_warn_gpio < 0) {
		dev_info(info->dev, "%s: No smpl_warn pin\n", __func__);
		info->smpl_warn_gpio = -1;
	}

	info->ocp_warn_gpio = of_get_named_gpio(dn, "if-pmic-ocp-warn-gpio", 0);
	if (info->ocp_warn_gpio < 0) {
		dev_info(info->dev, "%s: No ocp_warn pin\n", __func__);
		info->ocp_warn_gpio = -1;
	}

	info->num_policy_cpus = of_property_count_u32_elems(dn, "policy_cpus");
	if (info->num_policy_cpus <= 0)
		return -EINVAL;

	info->policy_cpus = devm_kcalloc(info->dev, info->num_policy_cpus,
			sizeof(*info->policy_cpus), GFP_KERNEL);
	if (!info->policy_cpus)
		return -ENOMEM;

	ret = of_property_read_u32_array(dn, "policy_cpus", info->policy_cpus,
			info->num_policy_cpus);

	/* Get GPU freq_table from sgpu device tree */
	ret = of_property_read_u32(dn, "sgpu_phandle", &sgpu_phandle);
	if (ret) {
		dev_err(info->dev, "There is no sgpu in DT (%d)", ret);
		return -EINVAL;
	}

	sgpu_node = of_find_node_by_phandle(sgpu_phandle);
	if (!sgpu_node) {
		dev_err(info->dev, "It cannot find module node in DT");
		return -EINVAL;
	}

	gpu_freq_table_size =
		of_property_count_u32_elems(sgpu_node, "freq_table");
	if (gpu_freq_table_size <= 0)
		return -EINVAL;

	gpu_freq_table = devm_kcalloc(info->dev, gpu_freq_table_size,
			sizeof(*gpu_freq_table), GFP_KERNEL);
	if (!gpu_freq_table)
		return -ENOMEM;

	if (of_property_read_u32_array(sgpu_node, "freq_table",
				gpu_freq_table, gpu_freq_table_size))
		return -ENODATA;

	ret = fill_gpu_freq_table_in_descending_order(info);

	return ret;
}

static int init_cpufreq_dev(struct sec_pm_smpl_warn_info *info, unsigned int idx)
{
	struct cpufreq_dev *cdev;
	struct cpufreq_policy *policy;
	int ret;
	unsigned int freq, i;

	policy = cpufreq_cpu_get(info->policy_cpus[idx]);

	if (IS_ERR_OR_NULL(policy)) {
		dev_err(info->dev, "%s: cpufreq policy isn't valid: %pK\n",
				__func__, policy);
		return -EINVAL;
	}

	i = cpufreq_table_count_valid_entries(policy);
	if (!i) {
		dev_err(info->dev, "%s: CPUFreq table not found or has no valid entries\n",
				__func__);
		return -ENODEV;
	}

	cdev = devm_kzalloc(info->dev, sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->policy = policy;
	cdev->max_level = i - 1;

	cdev->freq_table = devm_kmalloc_array(info->dev, i,
			sizeof(*cdev->freq_table), GFP_KERNEL);
	if (!cdev->freq_table)
		return -ENOMEM;

	/* Fill freq_table in descending order of frequencies */
	for (i = 0, freq = -1; i <= cdev->max_level; i++) {
		freq = find_next_max_cpufreq(policy->freq_table, freq);
		cdev->freq_table[i].frequency = freq;

		/* Warn for duplicate entries */
		if (!freq)
			dev_warn(info->dev, "%s: table has duplicate entries\n", __func__);
		else
			dev_dbg(info->dev, "%s: freq:%u KHz\n", __func__, freq);
	}

	cdev->max_freq = cdev->freq_table[0].frequency;

	snprintf(cdev->qos_name, sizeof(cdev->qos_name), "smpl_warn_%u",
			info->policy_cpus[idx]);

	ret = freq_qos_tracer_add_request_name(cdev->qos_name,
			&policy->constraints,
			&cdev->qos_req, FREQ_QOS_MAX,
			cdev->freq_table[0].frequency);
	if (ret < 0) {
		pr_err("%s: Failed to add freq constraint (%d)\n", __func__,
				ret);
		return ret;
	}

	mutex_lock(&cpufreq_dev_list_lock);
	list_add(&cdev->node, &cpufreq_dev_list);
	mutex_unlock(&cpufreq_dev_list_lock);

	return 0;
}

static int sec_pm_smpl_warn_probe(struct platform_device *pdev)
{
	struct sec_pm_smpl_warn_info *info;
	unsigned int i;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	platform_set_drvdata(pdev, info);
	info->dev = &pdev->dev;

	ret = sec_pm_smpl_warn_parse_dt(info);
	if (ret) {
		dev_err(info->dev, "%s: fail to parse dt(%d)\n", __func__, ret);
		return ret;
	}

	for (i = 0; i < info->num_policy_cpus; i++) {
		ret = init_cpufreq_dev(info, i);
		if (ret < 0) {
			dev_err(info->dev, "%s: failt to initialize cpufreq_dev(%u)\n",
					__func__, i);
			return ret;
		}
	}

	exynos_pm_qos_add_request(&sec_pm_gpu_max_qos, PM_QOS_GPU_THROUGHPUT_MAX,
			PM_QOS_GPU_FREQ_MAX_DEFAULT_VALUE);

	if (info->ocp_warn_gpio >= 0) {
		info->ocp_warn_irq = gpio_to_irq(info->ocp_warn_gpio);
		ret = devm_request_threaded_irq(info->dev, info->ocp_warn_irq,
			NULL, sec_pm_ocp_warn_irq_thread,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"OCP_WARN", info);

		if (ret)
			dev_err(info->dev,
				"Failed to register OCP_WARN IRQ: %d\n", ret);
	}

	info->sec_dev = sec_device_create(info, "sec_pm_smpl_warn");
	if (IS_ERR(info->sec_dev))
		return -ENODEV;

	ret = sysfs_create_group(&info->sec_dev->kobj,
			&sec_pm_smpl_warn_sysfs_attr_group);
	if (ret) {
		sec_device_destroy(info->sec_dev->devt);
		return -ENODEV;
	}

	smpl_warn_init_done = true;

	return 0;
}

static int sec_pm_smpl_warn_remove(struct platform_device *pdev)
{
	struct cpufreq_dev *cdev;
	struct sec_pm_smpl_warn_info *info = platform_get_drvdata(pdev);

	dev_info(info->dev, "%s\n", __func__);

	mutex_lock(&cpufreq_dev_list_lock);
	list_for_each_entry(cdev, &cpufreq_dev_list, node) {
		list_del(&cdev->node);
		freq_qos_tracer_remove_request(&cdev->qos_req);
	}
	mutex_unlock(&cpufreq_dev_list_lock);

	exynos_pm_qos_remove_request(&sec_pm_gpu_max_qos);

	if (!IS_ERR_OR_NULL(info->sec_dev))
		sec_device_destroy(info->sec_dev->devt);

	return 0;
}

static const struct of_device_id sec_pm_smpl_warn_match[] = {
	{ .compatible = "samsung,sec-pm-smpl-warn", },
	{ },
};
MODULE_DEVICE_TABLE(of, sec_pm_smpl_warn_match);

static struct platform_driver sec_pm_smpl_warn_driver = {
	.driver = {
		.name = "sec-pm-smpl_warn",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sec_pm_smpl_warn_match),
	},
	.probe = sec_pm_smpl_warn_probe,
	.remove = sec_pm_smpl_warn_remove,
};

module_platform_driver(sec_pm_smpl_warn_driver);

MODULE_AUTHOR("Minsung Kim <ms925.kim@samsung.com>");
MODULE_DESCRIPTION("SEC PM SMPL_WARN Control");
MODULE_LICENSE("GPL");
