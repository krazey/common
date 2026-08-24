// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com/
//
// Exynos - CPU PMU(Power Management Unit) support

#include <linux/array_size.h>
#include <linux/bitmap.h>
#include <linux/cpuhotplug.h>
#include <linux/cpu_pm.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/init.h>
#include <linux/mfd/core.h>
#include <linux/mfd/syscon.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

#include <linux/soc/samsung/exynos-regs-pmu.h>
#include <linux/soc/samsung/exynos-pmu.h>

#include <asm/smp_plat.h>

#include "exynos-pmu.h"

/*
 * Samsung S-Boot distinguishes a reset from a power-off with INFORM2.
 * INFORM3 remains owned by the existing syscon-reboot-mode child.
 */
#define EXYNOS9810_PMU_INFORM2			0x0808
#define EXYNOS9810_POWER_OFF			0x00000000
#define EXYNOS9810_POWER_RESET			0x12345678

#define EXYNOS9810_PMU_CPU_INFORM_BASE		0x0860
#define EXYNOS9810_PMU_CPU_INFORM_STRIDE	0x0004
#define EXYNOS9810_CPU_INFORM_C2		0x00000001

#define EXYNOS9810_PMU_EVENT_ENABLE_GRP2		0x7f08
#define EXYNOS9810_GRP1_INTR_BID_UPEND		0x0108
#define EXYNOS9810_GRP1_INTR_BID_CLEAR		0x010c
#define EXYNOS9810_GRP2_INTR_BID_ENABLE		0x0200
#define EXYNOS9810_GRP2_INTR_BID_UPEND		0x0208
#define EXYNOS9810_GRP2_INTR_BID_CLEAR		0x020c

struct exynos_pmu_context {
	struct device *dev;
	const struct exynos_pmu_data *pmu_data;
	struct regmap *pmureg;
	struct regmap *pmuintrgen;
	/*
	 * Serialization lock for CPU hot plug and cpuidle ACPM hint
	 * programming. Also protects in_cpuhp, sys_insuspend & sys_inreboot
	 * flags.
	 */
	raw_spinlock_t cpupm_lock;
	unsigned long *in_cpuhp;
	bool sys_insuspend;
	bool sys_inreboot;
	/* Serializes Exynos9810 ACPM handshake register updates. */
	struct mutex exynos9810_cpu_lock;
	unsigned long *exynos9810_cpu_ready;
	int exynos9810_prepare_state;
	int exynos9810_online_state;
};

void __iomem *pmu_base_addr;
static struct exynos_pmu_context *pmu_context;
/* forward declaration */
static struct platform_driver exynos_pmu_driver;

void pmu_raw_writel(u32 val, u32 offset)
{
	writel_relaxed(val, pmu_base_addr + offset);
}

u32 pmu_raw_readl(u32 offset)
{
	return readl_relaxed(pmu_base_addr + offset);
}

void exynos_sys_powerdown_conf(enum sys_powerdown mode)
{
	unsigned int i;
	const struct exynos_pmu_data *pmu_data;

	if (!pmu_context || !pmu_context->pmu_data)
		return;

	pmu_data = pmu_context->pmu_data;

	if (pmu_data->powerdown_conf)
		pmu_data->powerdown_conf(mode);

	if (pmu_data->pmu_config) {
		for (i = 0; (pmu_data->pmu_config[i].offset != PMU_TABLE_END); i++)
			pmu_raw_writel(pmu_data->pmu_config[i].val[mode],
					pmu_data->pmu_config[i].offset);
	}

	if (pmu_data->powerdown_conf_extra)
		pmu_data->powerdown_conf_extra(mode);

	if (pmu_data->pmu_config_extra) {
		for (i = 0; pmu_data->pmu_config_extra[i].offset != PMU_TABLE_END; i++)
			pmu_raw_writel(pmu_data->pmu_config_extra[i].val[mode],
				       pmu_data->pmu_config_extra[i].offset);
	}
}

/*
 * Split the data between ARM architectures because it is relatively big
 * and useless on other arch.
 */
#ifdef CONFIG_EXYNOS_PMU_ARM_DRIVERS
#define exynos_pmu_data_arm_ptr(data)	(&data)
#else
#define exynos_pmu_data_arm_ptr(data)	NULL
#endif

static const struct regmap_config regmap_smccfg = {
	.name = "pmu_regs",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.use_single_read = true,
	.use_single_write = true,
	.reg_read = tensor_sec_reg_read,
	.reg_write = tensor_sec_reg_write,
	.reg_update_bits = tensor_sec_update_bits,
	.use_raw_spinlock = true,
};

static const struct regmap_config regmap_pmu_intr = {
	.name = "pmu_intr_gen",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.use_raw_spinlock = true,
};

/*
 * PMU platform driver and devicetree bindings.
 */
static const struct of_device_id exynos_pmu_of_device_ids[] = {
	{
		.compatible = "google,gs101-pmu",
		.data = &gs101_pmu_data,
	}, {
		.compatible = "samsung,exynos3250-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos3250_pmu_data),
	}, {
		.compatible = "samsung,exynos4210-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos4210_pmu_data),
	}, {
		.compatible = "samsung,exynos4212-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos4212_pmu_data),
	}, {
		.compatible = "samsung,exynos4412-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos4412_pmu_data),
	}, {
		.compatible = "samsung,exynos5250-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos5250_pmu_data),
	}, {
		.compatible = "samsung,exynos5410-pmu",
	}, {
		.compatible = "samsung,exynos5420-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos5420_pmu_data),
	}, {
		.compatible = "samsung,exynos5433-pmu",
	}, {
		.compatible = "samsung,exynos7-pmu",
	}, {
		.compatible = "samsung,exynos850-pmu",
	},
	{ /*sentinel*/ },
};

static const struct mfd_cell exynos_pmu_devs[] = {
	{ .name = "exynos-clkout", },
};

/**
 * exynos_get_pmu_regmap() - Obtain pmureg regmap
 *
 * Find the pmureg regmap previously configured in probe() and return regmap
 * pointer.
 *
 * Return: A pointer to regmap if found or ERR_PTR error value.
 */
struct regmap *exynos_get_pmu_regmap(void)
{
	struct device_node *np = of_find_matching_node(NULL,
						      exynos_pmu_of_device_ids);
	if (np)
		return exynos_get_pmu_regmap_by_phandle(np, NULL);
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(exynos_get_pmu_regmap);

/**
 * exynos_get_pmu_regmap_by_phandle() - Obtain pmureg regmap via phandle
 * @np: Device node holding PMU phandle property
 * @propname: Name of property holding phandle value
 *
 * Find the pmureg regmap previously configured in probe() and return regmap
 * pointer.
 *
 * Return: A pointer to regmap if found or ERR_PTR error value.
 */
struct regmap *exynos_get_pmu_regmap_by_phandle(struct device_node *np,
						const char *propname)
{
	struct device_node *pmu_np;
	struct device *dev;

	if (propname)
		pmu_np = of_parse_phandle(np, propname, 0);
	else
		pmu_np = np;

	if (!pmu_np)
		return ERR_PTR(-ENODEV);

	/*
	 * Determine if exynos-pmu device has probed and therefore regmap
	 * has been created and can be returned to the caller. Otherwise we
	 * return -EPROBE_DEFER.
	 */
	dev = driver_find_device_by_of_node(&exynos_pmu_driver.driver,
					    (void *)pmu_np);

	if (propname)
		of_node_put(pmu_np);

	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);

	put_device(dev);

	return syscon_node_to_regmap(pmu_np);
}
EXPORT_SYMBOL_GPL(exynos_get_pmu_regmap_by_phandle);

#ifdef CONFIG_EXYNOS9810_MONGOOSE_CPUS
struct exynos9810_cpu_reg_update {
	unsigned int reg;
	unsigned int mask;
	unsigned int value;
};

static const struct exynos9810_cpu_reg_update
exynos9810_pmu_cpu_defaults[] = {
	{ 0x1088, GENMASK(31, 24), 0x8d << 24 },
	{ 0x2114, GENMASK(1, 0), 2 },
	{ 0x2180, GENMASK(1, 0), 2 },
	{ 0x21ec, GENMASK(1, 0), 2 },
	{ 0x2258, GENMASK(1, 0), 2 },
	{ 0x2384, GENMASK(1, 0), 2 },
	{ 0x23c8, GENMASK(1, 0), 2 },
	{ 0x2418, GENMASK(1, 0), 2 },
	{ 0x2468, GENMASK(1, 0), 2 },
	{ 0x24b8, GENMASK(1, 0), 2 },
	{ 0x25a0, GENMASK(1, 0), 2 },
};

static const struct exynos9810_cpu_reg_update
exynos9810_core_cmu_defaults[] = {
	{ 0x0850, BIT(0), BIT(0) },
	{ 0x0820, U32_MAX, 1 },
};

static const struct exynos9810_cpu_reg_update
exynos9810_cpucl0_cmu_defaults[] = {
	{ 0x0850, GENMASK(6, 0), 1 },
	{ 0x0840, BIT(0), BIT(0) },
	{ 0x0834, U32_MAX, 1 },
	{ 0x0820, U32_MAX, U32_MAX },
};

static const struct exynos9810_cpu_reg_update
exynos9810_cpucl1_cmu_defaults[] = {
	{ 0x0850, GENMASK(6, 0), 0 },
	{ 0x0838, GENMASK(9, 0), 0x201 },
	{ 0x0820, U32_MAX, U32_MAX },
};

static const struct exynos9810_cpu_reg_update
exynos9810_core_sysreg_defaults[] = {
	{ 0x0104, U32_MAX, 0xfffffff0 },
};

static const struct exynos9810_cpu_reg_update
exynos9810_cluster_sysreg_defaults[] = {
	{ 0x0104, U32_MAX, U32_MAX },
};

static bool exynos9810_cpu_system_initialized;

static int __init
exynos9810_apply_cpu_defaults(struct regmap *map,
			     const struct exynos9810_cpu_reg_update *updates,
			     size_t count)
{
	size_t i;
	int ret;

	for (i = 0; i < count; i++) {
		ret = regmap_update_bits(map, updates[i].reg,
					 updates[i].mask, updates[i].value);
		if (ret)
			return ret;
	}

	return 0;
}

int __init exynos9810_cpu_system_init(void)
{
	struct regmap *cpucl0_cmu;
	struct regmap *cpucl0_sysreg;
	struct regmap *cpucl1_cmu;
	struct regmap *cpucl1_sysreg;
	struct regmap *core_cmu;
	struct regmap *core_sysreg;
	struct device_node *pmu_np;
	struct regmap *pmu;
	unsigned int value;
	int ret;

	if (!of_machine_is_compatible("samsung,exynos9810"))
		return 0;
	if (READ_ONCE(exynos9810_cpu_system_initialized))
		return 0;

	pmu_np = of_find_compatible_node(NULL, NULL,
					 "samsung,exynos9810-pmu");
	if (!pmu_np)
		return -ENODEV;

	pmu = syscon_node_to_regmap(pmu_np);
	core_cmu = syscon_regmap_lookup_by_phandle(
		pmu_np, "samsung,core-cmu-syscon");
	core_sysreg = syscon_regmap_lookup_by_phandle(
		pmu_np, "samsung,core-sysreg-syscon");
	cpucl0_cmu = syscon_regmap_lookup_by_phandle(
		pmu_np, "samsung,cpucl0-cmu-syscon");
	cpucl0_sysreg = syscon_regmap_lookup_by_phandle(
		pmu_np, "samsung,cpucl0-sysreg-syscon");
	cpucl1_cmu = syscon_regmap_lookup_by_phandle(
		pmu_np, "samsung,cpucl1-cmu-syscon");
	cpucl1_sysreg = syscon_regmap_lookup_by_phandle(
		pmu_np, "samsung,cpucl1-sysreg-syscon");

	if (IS_ERR(pmu) || IS_ERR(core_cmu) || IS_ERR(core_sysreg) ||
	    IS_ERR(cpucl0_cmu) || IS_ERR(cpucl0_sysreg) ||
	    IS_ERR(cpucl1_cmu) || IS_ERR(cpucl1_sysreg)) {
		ret = -ENODEV;
		goto out_put;
	}

	ret = exynos9810_apply_cpu_defaults(
		pmu, exynos9810_pmu_cpu_defaults,
		ARRAY_SIZE(exynos9810_pmu_cpu_defaults));
	if (ret)
		goto out_put;
	ret = exynos9810_apply_cpu_defaults(
		core_cmu, exynos9810_core_cmu_defaults,
		ARRAY_SIZE(exynos9810_core_cmu_defaults));
	if (ret)
		goto out_put;
	ret = exynos9810_apply_cpu_defaults(
		cpucl0_cmu, exynos9810_cpucl0_cmu_defaults,
		ARRAY_SIZE(exynos9810_cpucl0_cmu_defaults));
	if (ret)
		goto out_put;
	ret = exynos9810_apply_cpu_defaults(
		cpucl1_cmu, exynos9810_cpucl1_cmu_defaults,
		ARRAY_SIZE(exynos9810_cpucl1_cmu_defaults));
	if (ret)
		goto out_put;
	ret = exynos9810_apply_cpu_defaults(
		core_sysreg, exynos9810_core_sysreg_defaults,
		ARRAY_SIZE(exynos9810_core_sysreg_defaults));
	if (ret)
		goto out_put;
	ret = exynos9810_apply_cpu_defaults(
		cpucl0_sysreg, exynos9810_cluster_sysreg_defaults,
		ARRAY_SIZE(exynos9810_cluster_sysreg_defaults));
	if (ret)
		goto out_put;
	ret = exynos9810_apply_cpu_defaults(
		cpucl1_sysreg, exynos9810_cluster_sysreg_defaults,
		ARRAY_SIZE(exynos9810_cluster_sysreg_defaults));
	if (ret)
		goto out_put;

	ret = regmap_read(cpucl1_cmu, 0x0820, &value);
	if (!ret) {
		WRITE_ONCE(exynos9810_cpu_system_initialized, true);
		pr_info("Exynos9810 CPU system defaults initialized\n");
	}

out_put:
	of_node_put(pmu_np);
	return ret;
}

static bool exynos9810_is_mongoose_cpu(unsigned int cpu)
{
	return MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 1) == 1;
}

static int exynos9810_cpu_power_ids(unsigned int cpu, unsigned int *inform,
				    u32 *mask)
{
	u64 mpidr = cpu_logical_map(cpu);
	unsigned int cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	unsigned int core = MPIDR_AFFINITY_LEVEL(mpidr, 0);

	if (cluster > 1 || core > 3)
		return -EINVAL;

	/*
	 * CAL numbers CPUCL0 as cores 0-3 and CPUCL1 as cores 4-7,
	 * while the BID registers use bits 4-7 and 0-3 respectively.
	 */
	*inform = (cluster << 2) | core;
	*mask = BIT(core + (cluster ? 0 : 4));

	return 0;
}

bool exynos9810_cpu_power_ready(unsigned int cpu)
{
	struct exynos_pmu_context *context = READ_ONCE(pmu_context);

	if (!exynos9810_is_mongoose_cpu(cpu))
		return true;

	if (context && context->exynos9810_cpu_ready)
		return test_bit(cpu, context->exynos9810_cpu_ready);

	return READ_ONCE(exynos9810_cpu_system_initialized);
}

static int __exynos9810_cpu_power_on(struct exynos_pmu_context *context,
				     unsigned int cpu)
{
	u32 enable;
	u32 event;
	unsigned int inform;
	u32 mask;
	u32 pending;
	int ret;

	ret = exynos9810_cpu_power_ids(cpu, &inform, &mask);
	if (ret)
		return ret;

	ret = regmap_write(context->pmureg,
			   EXYNOS9810_PMU_CPU_INFORM_BASE +
			   inform * EXYNOS9810_PMU_CPU_INFORM_STRIDE, 0);
	if (ret)
		return ret;

	ret = regmap_update_bits(context->pmuintrgen,
				 EXYNOS9810_GRP2_INTR_BID_ENABLE, mask, 0);
	if (ret)
		return ret;

	ret = regmap_read(context->pmuintrgen,
			  EXYNOS9810_GRP2_INTR_BID_UPEND, &pending);
	if (ret)
		return ret;

	if (pending & mask) {
		ret = regmap_write(context->pmuintrgen,
				   EXYNOS9810_GRP2_INTR_BID_CLEAR, mask);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits(context->pmureg,
				 EXYNOS9810_PMU_EVENT_ENABLE_GRP2, mask, 0);
	if (ret)
		return ret;

	ret = regmap_read(context->pmuintrgen,
			  EXYNOS9810_GRP2_INTR_BID_ENABLE, &enable);
	if (ret)
		return ret;
	ret = regmap_read(context->pmuintrgen,
			  EXYNOS9810_GRP2_INTR_BID_UPEND, &pending);
	if (ret)
		return ret;
	ret = regmap_read(context->pmureg,
			  EXYNOS9810_PMU_EVENT_ENABLE_GRP2, &event);
	if (ret)
		return ret;

	set_bit(cpu, context->exynos9810_cpu_ready);
	dev_info(context->dev,
		 "CPU%u power-on handshake: enable=%#x pending=%#x event=%#x\n",
		 cpu, enable, pending, event);

	return 0;
}

static int __exynos9810_cpu_power_off(struct exynos_pmu_context *context,
				      unsigned int cpu)
{
	u32 enable;
	u32 event;
	unsigned int inform;
	u32 mask;
	u32 pending;
	int ret;

	ret = exynos9810_cpu_power_ids(cpu, &inform, &mask);
	if (ret)
		return ret;

	ret = regmap_write(context->pmureg,
			   EXYNOS9810_PMU_CPU_INFORM_BASE +
			   inform * EXYNOS9810_PMU_CPU_INFORM_STRIDE,
			   EXYNOS9810_CPU_INFORM_C2);
	if (ret)
		return ret;

	ret = regmap_update_bits(context->pmuintrgen,
				 EXYNOS9810_GRP2_INTR_BID_ENABLE, mask, mask);
	if (ret)
		return ret;

	ret = regmap_read(context->pmuintrgen,
			  EXYNOS9810_GRP1_INTR_BID_UPEND, &pending);
	if (ret)
		return ret;

	if (pending & mask) {
		ret = regmap_write(context->pmuintrgen,
				   EXYNOS9810_GRP1_INTR_BID_CLEAR, mask);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits(context->pmureg,
				 EXYNOS9810_PMU_EVENT_ENABLE_GRP2,
				 mask, mask);
	if (ret)
		return ret;

	ret = regmap_read(context->pmuintrgen,
			  EXYNOS9810_GRP2_INTR_BID_ENABLE, &enable);
	if (ret)
		return ret;
	ret = regmap_read(context->pmuintrgen,
			  EXYNOS9810_GRP1_INTR_BID_UPEND, &pending);
	if (ret)
		return ret;
	ret = regmap_read(context->pmureg,
			  EXYNOS9810_PMU_EVENT_ENABLE_GRP2, &event);
	if (ret)
		return ret;

	clear_bit(cpu, context->exynos9810_cpu_ready);
	dev_info(context->dev,
		 "CPU%u power-off handshake: enable=%#x pending=%#x event=%#x\n",
		 cpu, enable, pending, event);

	return 0;
}

static int exynos9810_cpuhp_prepare(unsigned int cpu)
{
	struct exynos_pmu_context *context = READ_ONCE(pmu_context);
	int ret;

	if (!context || !context->pmuintrgen ||
	    !context->exynos9810_cpu_ready)
		return -ENODEV;

	mutex_lock(&context->exynos9810_cpu_lock);
	ret = __exynos9810_cpu_power_on(context, cpu);
	mutex_unlock(&context->exynos9810_cpu_lock);
	if (ret)
		dev_err(context->dev,
			"CPU%u power-on handshake failed: %d\n", cpu, ret);

	return ret;
}

static int exynos9810_cpuhp_online(unsigned int cpu)
{
	struct exynos_pmu_context *context = READ_ONCE(pmu_context);

	if (!context || !context->pmuintrgen ||
	    !context->exynos9810_cpu_ready)
		return -ENODEV;

	if (test_bit(cpu, context->exynos9810_cpu_ready))
		return 0;

	return exynos9810_cpuhp_prepare(cpu);
}

static int exynos9810_cpuhp_offline(unsigned int cpu)
{
	struct exynos_pmu_context *context = READ_ONCE(pmu_context);
	int ret;

	if (!context || !context->pmuintrgen ||
	    !context->exynos9810_cpu_ready)
		return -ENODEV;

	mutex_lock(&context->exynos9810_cpu_lock);
	ret = __exynos9810_cpu_power_off(context, cpu);
	mutex_unlock(&context->exynos9810_cpu_lock);
	if (ret)
		dev_err(context->dev,
			"CPU%u power-off handshake failed: %d\n", cpu, ret);

	return ret;
}

static int exynos9810_cpuhp_unprepare(unsigned int cpu)
{
	struct exynos_pmu_context *context = READ_ONCE(pmu_context);

	if (context && context->exynos9810_cpu_ready)
		clear_bit(cpu, context->exynos9810_cpu_ready);

	return 0;
}

static void exynos9810_remove_cpuhp(void *data)
{
	struct exynos_pmu_context *context = data;

	cpuhp_remove_state_nocalls(context->exynos9810_online_state);
	cpuhp_remove_state_nocalls(context->exynos9810_prepare_state);
}

static int exynos9810_setup_cpuhp(struct device *dev)
{
	struct exynos_pmu_context *context = pmu_context;
	int ret;

	context->pmuintrgen =
		syscon_regmap_lookup_by_phandle(dev->of_node,
						"samsung,pmu-intr-gen-syscon");
	if (IS_ERR(context->pmuintrgen))
		return dev_err_probe(dev, PTR_ERR(context->pmuintrgen),
				     "failed to get PMU interrupt generator\n");

	context->exynos9810_cpu_ready =
		devm_bitmap_zalloc(dev, num_possible_cpus(), GFP_KERNEL);
	if (!context->exynos9810_cpu_ready)
		return -ENOMEM;

	mutex_init(&context->exynos9810_cpu_lock);

	ret = cpuhp_setup_state_nocalls(CPUHP_BP_PREPARE_DYN,
					"soc/exynos9810:prepare",
					exynos9810_cpuhp_prepare,
					exynos9810_cpuhp_unprepare);
	if (ret < 0)
		return ret;
	context->exynos9810_prepare_state = ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"soc/exynos9810:online",
				exynos9810_cpuhp_online,
				exynos9810_cpuhp_offline);
	if (ret < 0) {
		cpuhp_remove_state_nocalls(context->exynos9810_prepare_state);
		return ret;
	}
	context->exynos9810_online_state = ret;

	ret = devm_add_action_or_reset(dev, exynos9810_remove_cpuhp, context);
	if (ret)
		return ret;

	dev_info(dev, "Exynos9810 CPU power handshake registered\n");
	return 0;
}
#endif

/*
 * CPU_INFORM register "hint" values are required to be programmed in addition to
 * the standard PSCI calls to have functional CPU hotplug and CPU idle states.
 * This is required to workaround limitations in the el3mon/ACPM firmware.
 */
#define CPU_INFORM_CLEAR	0
#define CPU_INFORM_C2		1

/*
 * __gs101_cpu_pmu_ prefix functions are common code shared by CPU PM notifiers
 * (CPUIdle) and CPU hotplug callbacks. Functions should be called with IRQs
 * disabled and cpupm_lock held.
 */
static int __gs101_cpu_pmu_online(unsigned int cpu)
	__must_hold(&pmu_context->cpupm_lock)
{
	unsigned int cpuhint = smp_processor_id();
	u32 reg, mask;

	/* clear cpu inform hint */
	regmap_write(pmu_context->pmureg, GS101_CPU_INFORM(cpuhint),
		     CPU_INFORM_CLEAR);

	mask = BIT(cpu);

	regmap_update_bits(pmu_context->pmuintrgen, GS101_GRP2_INTR_BID_ENABLE,
			   mask, (0 << cpu));

	regmap_read(pmu_context->pmuintrgen, GS101_GRP2_INTR_BID_UPEND, &reg);

	regmap_write(pmu_context->pmuintrgen, GS101_GRP2_INTR_BID_CLEAR,
		     reg & mask);

	return 0;
}

/* Called from CPU PM notifier (CPUIdle code path) with IRQs disabled */
static int gs101_cpu_pmu_online(void)
{
	int cpu;

	raw_spin_lock(&pmu_context->cpupm_lock);

	if (pmu_context->sys_inreboot) {
		raw_spin_unlock(&pmu_context->cpupm_lock);
		return NOTIFY_OK;
	}

	cpu = smp_processor_id();
	__gs101_cpu_pmu_online(cpu);
	raw_spin_unlock(&pmu_context->cpupm_lock);

	return NOTIFY_OK;
}

/* Called from CPU hot plug callback with IRQs enabled */
static int gs101_cpuhp_pmu_online(unsigned int cpu)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&pmu_context->cpupm_lock, flags);

	__gs101_cpu_pmu_online(cpu);
	/*
	 * Mark this CPU as having finished the hotplug.
	 * This means this CPU can now enter C2 idle state.
	 */
	clear_bit(cpu, pmu_context->in_cpuhp);
	raw_spin_unlock_irqrestore(&pmu_context->cpupm_lock, flags);

	return 0;
}

/* Common function shared by both CPU hot plug and CPUIdle */
static int __gs101_cpu_pmu_offline(unsigned int cpu)
	__must_hold(&pmu_context->cpupm_lock)
{
	unsigned int cpuhint = smp_processor_id();
	u32 reg, mask;

	/* set cpu inform hint */
	regmap_write(pmu_context->pmureg, GS101_CPU_INFORM(cpuhint),
		     CPU_INFORM_C2);

	mask = BIT(cpu);
	regmap_update_bits(pmu_context->pmuintrgen, GS101_GRP2_INTR_BID_ENABLE,
			   mask, BIT(cpu));

	regmap_read(pmu_context->pmuintrgen, GS101_GRP1_INTR_BID_UPEND, &reg);
	regmap_write(pmu_context->pmuintrgen, GS101_GRP1_INTR_BID_CLEAR,
		     reg & mask);

	mask = (BIT(cpu + 8));
	regmap_read(pmu_context->pmuintrgen, GS101_GRP1_INTR_BID_UPEND, &reg);
	regmap_write(pmu_context->pmuintrgen, GS101_GRP1_INTR_BID_CLEAR,
		     reg & mask);

	return 0;
}

/* Called from CPU PM notifier (CPUIdle code path) with IRQs disabled */
static int gs101_cpu_pmu_offline(void)
{
	int cpu;

	raw_spin_lock(&pmu_context->cpupm_lock);
	cpu = smp_processor_id();

	if (test_bit(cpu, pmu_context->in_cpuhp)) {
		raw_spin_unlock(&pmu_context->cpupm_lock);
		return NOTIFY_BAD;
	}

	/* Ignore CPU_PM_ENTER event in reboot or suspend sequence. */
	if (pmu_context->sys_insuspend || pmu_context->sys_inreboot) {
		raw_spin_unlock(&pmu_context->cpupm_lock);
		return NOTIFY_OK;
	}

	__gs101_cpu_pmu_offline(cpu);
	raw_spin_unlock(&pmu_context->cpupm_lock);

	return NOTIFY_OK;
}

/* Called from CPU hot plug callback with IRQs enabled */
static int gs101_cpuhp_pmu_offline(unsigned int cpu)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&pmu_context->cpupm_lock, flags);
	/*
	 * Mark this CPU as entering hotplug. So as not to confuse
	 * ACPM the CPU entering hotplug should not enter C2 idle state.
	 */
	set_bit(cpu, pmu_context->in_cpuhp);
	__gs101_cpu_pmu_offline(cpu);

	raw_spin_unlock_irqrestore(&pmu_context->cpupm_lock, flags);

	return 0;
}

static int gs101_cpu_pm_notify_callback(struct notifier_block *self,
					unsigned long action, void *v)
{
	switch (action) {
	case CPU_PM_ENTER:
		return gs101_cpu_pmu_offline();

	case CPU_PM_EXIT:
		return gs101_cpu_pmu_online();
	}

	return NOTIFY_OK;
}

static struct notifier_block gs101_cpu_pm_notifier = {
	.notifier_call = gs101_cpu_pm_notify_callback,
	/*
	 * We want to be called first, as the ACPM hint and handshake is what
	 * puts the CPU into C2.
	 */
	.priority = INT_MAX
};

static int exynos9810_reboot_notifier(struct notifier_block *nb,
				      unsigned long event, void *cmd)
{
	u32 value;
	int ret;

	switch (event) {
	case SYS_RESTART:
		value = EXYNOS9810_POWER_RESET;
		break;
	case SYS_POWER_OFF:
		value = EXYNOS9810_POWER_OFF;
		break;
	default:
		return NOTIFY_DONE;
	}

	ret = regmap_write(pmu_context->pmureg, EXYNOS9810_PMU_INFORM2, value);
	if (ret)
		dev_err(pmu_context->dev,
			"failed to write Exynos9810 INFORM2: %d\n", ret);

	return NOTIFY_OK;
}

static struct notifier_block exynos9810_reboot_nb = {
	.notifier_call = exynos9810_reboot_notifier,
	.priority = INT_MAX,
};

static int exynos_cpupm_reboot_notifier(struct notifier_block *nb,
					unsigned long event, void *v)
{
	unsigned long flags;

	switch (event) {
	case SYS_POWER_OFF:
	case SYS_RESTART:
		raw_spin_lock_irqsave(&pmu_context->cpupm_lock, flags);
		pmu_context->sys_inreboot = true;
		raw_spin_unlock_irqrestore(&pmu_context->cpupm_lock, flags);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block exynos_cpupm_reboot_nb = {
	.priority = INT_MAX,
	.notifier_call = exynos_cpupm_reboot_notifier,
};

static int setup_cpuhp_and_cpuidle(struct device *dev)
{
	struct device_node *intr_gen_node;
	struct resource intrgen_res;
	void __iomem *virt_addr;
	int ret, cpu;

	intr_gen_node = of_parse_phandle(dev->of_node,
					 "google,pmu-intr-gen-syscon", 0);
	if (!intr_gen_node) {
		/*
		 * To maintain support for older DTs that didn't specify syscon
		 * phandle just issue a warning rather than fail to probe.
		 */
		dev_warn(dev, "pmu-intr-gen syscon unavailable\n");
		return 0;
	}

	/*
	 * To avoid lockdep issues (CPU PM notifiers use raw spinlocks) create
	 * a mmio regmap for pmu-intr-gen that uses raw spinlocks instead of
	 * syscon provided regmap.
	 */
	ret = of_address_to_resource(intr_gen_node, 0, &intrgen_res);
	of_node_put(intr_gen_node);

	virt_addr = devm_ioremap(dev, intrgen_res.start,
				 resource_size(&intrgen_res));
	if (!virt_addr)
		return -ENOMEM;

	pmu_context->pmuintrgen = devm_regmap_init_mmio(dev, virt_addr,
							&regmap_pmu_intr);
	if (IS_ERR(pmu_context->pmuintrgen)) {
		dev_err(dev, "failed to initialize pmu-intr-gen regmap\n");
		return PTR_ERR(pmu_context->pmuintrgen);
	}

	/* register custom mmio regmap with syscon */
	ret = of_syscon_register_regmap(intr_gen_node,
					pmu_context->pmuintrgen);
	if (ret)
		return ret;

	pmu_context->in_cpuhp = devm_bitmap_zalloc(dev, num_possible_cpus(),
						   GFP_KERNEL);
	if (!pmu_context->in_cpuhp)
		return -ENOMEM;

	/* set PMU to power on */
	for_each_online_cpu(cpu)
		gs101_cpuhp_pmu_online(cpu);

	/* register CPU hotplug callbacks */
	cpuhp_setup_state(CPUHP_BP_PREPARE_DYN,	"soc/exynos-pmu:prepare",
			  gs101_cpuhp_pmu_online, NULL);

	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "soc/exynos-pmu:online",
			  NULL, gs101_cpuhp_pmu_offline);

	/* register CPU PM notifiers for cpuidle */
	cpu_pm_register_notifier(&gs101_cpu_pm_notifier);
	register_reboot_notifier(&exynos_cpupm_reboot_nb);
	return 0;
}

static int exynos_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap_config pmu_regmcfg;
	struct regmap *regmap;
	struct resource *res;
	int ret;

	pmu_base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pmu_base_addr))
		return PTR_ERR(pmu_base_addr);

	pmu_context = devm_kzalloc(&pdev->dev,
			sizeof(struct exynos_pmu_context),
			GFP_KERNEL);
	if (!pmu_context)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	pmu_context->pmu_data = of_device_get_match_data(dev);

	/* For SoCs that secure PMU register writes use custom regmap */
	if (pmu_context->pmu_data && pmu_context->pmu_data->pmu_secure) {
		pmu_regmcfg = regmap_smccfg;
		pmu_regmcfg.max_register = resource_size(res) -
					   pmu_regmcfg.reg_stride;
		pmu_regmcfg.wr_table = pmu_context->pmu_data->wr_table;
		pmu_regmcfg.rd_table = pmu_context->pmu_data->rd_table;

		/* Need physical address for SMC call */
		regmap = devm_regmap_init(dev, NULL,
					  (void *)(uintptr_t)res->start,
					  &pmu_regmcfg);

		if (IS_ERR(regmap))
			return dev_err_probe(&pdev->dev, PTR_ERR(regmap),
					     "regmap init failed\n");

		ret = of_syscon_register_regmap(dev->of_node, regmap);
		if (ret)
			return ret;
	} else {
		/* let syscon create mmio regmap */
		regmap = syscon_node_to_regmap(dev->of_node);
		if (IS_ERR(regmap))
			return dev_err_probe(&pdev->dev, PTR_ERR(regmap),
					     "syscon_node_to_regmap failed\n");
	}

	pmu_context->pmureg = regmap;
	pmu_context->dev = dev;
	raw_spin_lock_init(&pmu_context->cpupm_lock);
	pmu_context->sys_inreboot = false;
	pmu_context->sys_insuspend = false;

	if (of_device_is_compatible(dev->of_node,
				    "samsung,exynos9810-pmu")) {
		ret = devm_register_reboot_notifier(dev, &exynos9810_reboot_nb);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to register Exynos9810 reboot notifier\n");

#ifdef CONFIG_EXYNOS9810_MONGOOSE_CPUS
		ret = exynos9810_setup_cpuhp(dev);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to register CPU power handshake\n");
#endif
	}

	if (pmu_context->pmu_data && pmu_context->pmu_data->pmu_cpuhp) {
		ret = setup_cpuhp_and_cpuidle(dev);
		if (ret)
			return ret;
	}

	if (pmu_context->pmu_data && pmu_context->pmu_data->pmu_init)
		pmu_context->pmu_data->pmu_init();

	platform_set_drvdata(pdev, pmu_context);

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, exynos_pmu_devs,
				   ARRAY_SIZE(exynos_pmu_devs), NULL, 0, NULL);
	if (ret)
		return ret;

	if (devm_of_platform_populate(dev))
		dev_err(dev, "Error populating children, reboot and poweroff might not work properly\n");

	dev_dbg(dev, "Exynos PMU Driver probe done\n");
	return 0;
}

static int exynos_cpupm_suspend_noirq(struct device *dev)
{
	raw_spin_lock(&pmu_context->cpupm_lock);
	pmu_context->sys_insuspend = true;
	raw_spin_unlock(&pmu_context->cpupm_lock);
	return 0;
}

static int exynos_cpupm_resume_noirq(struct device *dev)
{
	raw_spin_lock(&pmu_context->cpupm_lock);
	pmu_context->sys_insuspend = false;
	raw_spin_unlock(&pmu_context->cpupm_lock);
	return 0;
}

static const struct dev_pm_ops cpupm_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(exynos_cpupm_suspend_noirq,
				  exynos_cpupm_resume_noirq)
};

static struct platform_driver exynos_pmu_driver = {
	.driver  = {
		.name   = "exynos-pmu",
		.of_match_table = exynos_pmu_of_device_ids,
		.pm = pm_sleep_ptr(&cpupm_pm_ops),
	},
	.probe = exynos_pmu_probe,
};

static int __init exynos_pmu_init(void)
{
	return platform_driver_register(&exynos_pmu_driver);

}
postcore_initcall(exynos_pmu_init);
