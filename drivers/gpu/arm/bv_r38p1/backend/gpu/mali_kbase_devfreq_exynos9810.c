// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exynos9810 legacy GPU frequency sysfs compatibility
 *
 * Lineage's existing power HAL uses the Samsung GPEX clock interface under
 * /sys/kernel/gpu. Translate those kHz requests into the standard devfreq
 * PM QoS constraints used by the mainline Mali driver.
 */

#include <mali_kbase.h>

#include <linux/devfreq.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/units.h>

#include "mali_kbase_devfreq.h"

struct kbase_exynos9810_gpu_clock {
	struct kobject kobj;
	struct kbase_device *kbdev;
};

static struct kbase_exynos9810_gpu_clock *
kbase_exynos9810_gpu_clock_from_kobj(struct kobject *kobj)
{
	return container_of(kobj, struct kbase_exynos9810_gpu_clock, kobj);
}

static int
kbase_exynos9810_gpu_clock_range(struct kbase_exynos9810_gpu_clock *clock,
				 unsigned long *min_freq,
				 unsigned long *max_freq)
{
	struct devfreq *devfreq = READ_ONCE(clock->kbdev->devfreq);

	if (!devfreq)
		return -ENODEV;

	mutex_lock(&devfreq->lock);
	devfreq_get_freq_range(devfreq, min_freq, max_freq);
	mutex_unlock(&devfreq->lock);

	return 0;
}

static ssize_t gpu_min_clock_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct kbase_exynos9810_gpu_clock *clock =
		kbase_exynos9810_gpu_clock_from_kobj(kobj);
	unsigned long min_freq;
	unsigned long max_freq;
	int ret;

	ret = kbase_exynos9810_gpu_clock_range(clock, &min_freq,
					       &max_freq);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%lu\n", min_freq / HZ_PER_KHZ);
}

static ssize_t gpu_max_clock_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct kbase_exynos9810_gpu_clock *clock =
		kbase_exynos9810_gpu_clock_from_kobj(kobj);
	unsigned long min_freq;
	unsigned long max_freq;
	int ret;

	ret = kbase_exynos9810_gpu_clock_range(clock, &min_freq,
					       &max_freq);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%lu\n", max_freq / HZ_PER_KHZ);
}

static ssize_t gpu_min_clock_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	struct kbase_exynos9810_gpu_clock *clock =
		kbase_exynos9810_gpu_clock_from_kobj(kobj);
	struct devfreq *devfreq = READ_ONCE(clock->kbdev->devfreq);
	unsigned long freq_khz;
	int ret;

	if (!devfreq)
		return -ENODEV;
	if (!dev_pm_qos_request_active(&devfreq->user_min_freq_req))
		return -EAGAIN;

	ret = kstrtoul(buf, 0, &freq_khz);
	if (ret)
		return ret;
	if (freq_khz > S32_MAX)
		return -ERANGE;

	ret = dev_pm_qos_update_request(&devfreq->user_min_freq_req,
					(s32)freq_khz);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t gpu_max_clock_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	struct kbase_exynos9810_gpu_clock *clock =
		kbase_exynos9810_gpu_clock_from_kobj(kobj);
	struct devfreq *devfreq = READ_ONCE(clock->kbdev->devfreq);
	unsigned long freq_khz;
	s32 request;
	int ret;

	if (!devfreq)
		return -ENODEV;
	if (!dev_pm_qos_request_active(&devfreq->user_max_freq_req))
		return -EAGAIN;

	ret = kstrtoul(buf, 0, &freq_khz);
	if (ret)
		return ret;
	if (freq_khz > S32_MAX)
		return -ERANGE;

	request = freq_khz ? (s32)freq_khz :
		PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE;
	ret = dev_pm_qos_update_request(&devfreq->user_max_freq_req, request);
	if (ret < 0)
		return ret;

	return count;
}

static struct kobj_attribute gpu_min_clock_attr =
	__ATTR(gpu_min_clock, 0644, gpu_min_clock_show, gpu_min_clock_store);
static struct kobj_attribute gpu_max_clock_attr =
	__ATTR(gpu_max_clock, 0644, gpu_max_clock_show, gpu_max_clock_store);

static struct attribute *kbase_exynos9810_gpu_clock_attrs[] = {
	&gpu_min_clock_attr.attr,
	&gpu_max_clock_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(kbase_exynos9810_gpu_clock);

static void kbase_exynos9810_gpu_clock_release(struct kobject *kobj)
{
	struct kbase_exynos9810_gpu_clock *clock =
		kbase_exynos9810_gpu_clock_from_kobj(kobj);

	kfree(clock);
}

static const struct kobj_type kbase_exynos9810_gpu_clock_ktype = {
	.release = kbase_exynos9810_gpu_clock_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = kbase_exynos9810_gpu_clock_groups,
};

int kbase_exynos9810_gpu_clock_init(struct kbase_device *kbdev)
{
	struct kbase_exynos9810_gpu_clock *clock;
	int ret;

	if (!of_machine_is_compatible("samsung,exynos9810"))
		return 0;

	clock = kzalloc_obj(*clock);
	if (!clock)
		return -ENOMEM;

	clock->kbdev = kbdev;
	ret = kobject_init_and_add(&clock->kobj,
				   &kbase_exynos9810_gpu_clock_ktype,
				   kernel_kobj, "gpu");
	if (ret) {
		kobject_put(&clock->kobj);
		return ret;
	}

	kbdev->exynos9810_gpu_clock = clock;
	dev_info(kbdev->dev, "legacy GPU clock controls are ready\n");
	return 0;
}

void kbase_exynos9810_gpu_clock_term(struct kbase_device *kbdev)
{
	struct kbase_exynos9810_gpu_clock *clock =
		kbdev->exynos9810_gpu_clock;

	if (!clock)
		return;

	kbdev->exynos9810_gpu_clock = NULL;
	kobject_put(&clock->kobj);
}
