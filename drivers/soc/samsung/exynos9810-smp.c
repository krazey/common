// SPDX-License-Identifier: GPL-2.0-only
/*
 * Deferred secondary CPU bring-up for Samsung Exynos9810.
 */

#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/smp.h>

static int __init exynos9810_online_secondary_cpus(void)
{
	unsigned int cpu;

	if (!of_machine_is_compatible("samsung,exynos9810"))
		return 0;

	for_each_present_cpu(cpu) {
		int ret;

		if (cpu_online(cpu))
			continue;

		ret = add_cpu(cpu);
		if (ret)
			pr_err("Exynos9810: failed to bring CPU%u online: %d\n",
			       cpu, ret);
		else
			pr_info("Exynos9810: brought CPU%u online after device init\n",
				cpu);
	}

	return 0;
}
late_initcall_sync(exynos9810_online_secondary_cpus);
