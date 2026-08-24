// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2013 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#define pr_fmt(fmt) "psci: " fmt

#include <linux/init.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/psci.h>
#include <linux/soc/samsung/exynos-pmu.h>

#include <uapi/linux/psci.h>

#include <asm/cpu_ops.h>
#include <asm/cputype.h>
#include <asm/errno.h>
#include <asm/memory.h>
#include <asm/setup.h>
#include <asm/smp_plat.h>

static bool cpu_psci_is_exynos9810_mongoose(unsigned int cpu)
{
	return of_machine_is_compatible("samsung,exynos9810") &&
	       MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 1) == 1;
}

#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
#define exynos9810_psci_marker(first, cpu) \
	exynos9810_boot_marker((first), '0' + (cpu))
#else
#define exynos9810_psci_marker(first, cpu) \
	do { (void)(first); (void)(cpu); } while (0)
#endif

static int __init cpu_psci_cpu_init(unsigned int cpu)
{
	return 0;
}

static int __init cpu_psci_cpu_prepare(unsigned int cpu)
{
	if (cpu_psci_is_exynos9810_mongoose(cpu)) {
		if (!IS_ENABLED(CONFIG_EXYNOS9810_MONGOOSE_CPUS))
			return -EOPNOTSUPP;

		if (!exynos9810_cpu_power_ready(cpu))
			return -EAGAIN;
	}

	if (!psci_ops.cpu_on) {
		pr_err("no cpu_on method, not booting CPU%d\n", cpu);
		return -ENODEV;
	}

	return 0;
}

static int cpu_psci_cpu_boot(unsigned int cpu)
{
	phys_addr_t pa_secondary_entry = __pa_symbol(secondary_entry);
	int err;

#ifdef CONFIG_EXYNOS9810_MONGOOSE_CPUS
	if (cpu_psci_is_exynos9810_mongoose(cpu))
		pa_secondary_entry = __pa_symbol(exynos9810_secondary_entry);
#endif

	exynos9810_psci_marker('P', cpu);
	err = psci_ops.cpu_on(cpu_logical_map(cpu), pa_secondary_entry);
	exynos9810_psci_marker('Q', cpu);
	if (err && err != -EPERM)
		pr_err("failed to boot CPU%d (%d)\n", cpu, err);

	return err;
}

#ifdef CONFIG_HOTPLUG_CPU
static bool cpu_psci_cpu_can_disable(unsigned int cpu)
{
	return !psci_tos_resident_on(cpu);
}

static int cpu_psci_cpu_disable(unsigned int cpu)
{
	/* Fail early if we don't have CPU_OFF support */
	if (!psci_ops.cpu_off)
		return -EOPNOTSUPP;

	/* Trusted OS will deny CPU_OFF */
	if (psci_tos_resident_on(cpu))
		return -EPERM;

	return 0;
}

static void cpu_psci_cpu_die(unsigned int cpu)
{
	/*
	 * There are no known implementations of PSCI actually using the
	 * power state field, pass a sensible default for now.
	 */
	u32 state = PSCI_POWER_STATE_TYPE_POWER_DOWN <<
		    PSCI_0_2_POWER_STATE_TYPE_SHIFT;

	psci_ops.cpu_off(state);
}

static int cpu_psci_cpu_kill(unsigned int cpu)
{
	int err;
	unsigned long start, end;

	if (!psci_ops.affinity_info)
		return 0;
	/*
	 * cpu_kill could race with cpu_die and we can
	 * potentially end up declaring this cpu undead
	 * while it is dying. So, try again a few times.
	 */

	start = jiffies;
	end = start + msecs_to_jiffies(100);
	do {
		err = psci_ops.affinity_info(cpu_logical_map(cpu), 0);
		if (err == PSCI_0_2_AFFINITY_LEVEL_OFF) {
			pr_info("CPU%d killed (polled %d ms)\n", cpu,
				jiffies_to_msecs(jiffies - start));
			return 0;
		}

		usleep_range(100, 1000);
	} while (time_before(jiffies, end));

	pr_warn("CPU%d may not have shut down cleanly (AFFINITY_INFO reports %d)\n",
			cpu, err);
	return -ETIMEDOUT;
}
#endif

const struct cpu_operations cpu_psci_ops = {
	.name		= "psci",
	.cpu_init	= cpu_psci_cpu_init,
	.cpu_prepare	= cpu_psci_cpu_prepare,
	.cpu_boot	= cpu_psci_cpu_boot,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_can_disable = cpu_psci_cpu_can_disable,
	.cpu_disable	= cpu_psci_cpu_disable,
	.cpu_die	= cpu_psci_cpu_die,
	.cpu_kill	= cpu_psci_cpu_kill,
#endif
};
