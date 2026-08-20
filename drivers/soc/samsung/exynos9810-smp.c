// SPDX-License-Identifier: GPL-2.0-only
/*
 * Deferred secondary CPU bring-up for Samsung Exynos9810.
 */

#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/smp.h>

#include <asm/smp_plat.h>

static bool __init exynos9810_acpm_ready(void)
{
	struct platform_device *pdev;
	struct device_node *np;
	bool ready;

	np = of_find_compatible_node(NULL, NULL,
				     "samsung,exynos-acpm-ipc");
	if (!np)
		return false;

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return false;

	ready = device_is_bound(&pdev->dev);
	put_device(&pdev->dev);

	return ready;
}

static bool __init exynos9810_secondary_supported(unsigned int cpu)
{
	u64 mpidr = cpu_logical_map(cpu);

	if (MPIDR_AFFINITY_LEVEL(mpidr, 1) != 1)
		return true;

	return MPIDR_AFFINITY_LEVEL(mpidr, 0) == 0;
}

static int __init exynos9810_online_secondary_cpus(void)
{
	unsigned int cpu;

	if (!of_machine_is_compatible("samsung,exynos9810"))
		return 0;

	if (!exynos9810_acpm_ready()) {
		pr_err("Exynos9810: ACPM is not ready; keeping secondary CPUs offline\n");
		return 0;
	}

	for_each_present_cpu(cpu) {
		int ret;

		if (cpu_online(cpu) ||
		    !exynos9810_secondary_supported(cpu))
			continue;

		ret = add_cpu(cpu);
		if (ret) {
			pr_err("Exynos9810: failed to bring CPU%u online: %d\n",
			       cpu, ret);
			break;
		}

		pr_info("Exynos9810: brought CPU%u online after ACPM init\n",
			cpu);
	}

	return 0;
}
late_initcall_sync(exynos9810_online_secondary_cpus);
