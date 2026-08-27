/* SPDX-License-Identifier: GPL-2.0-only */
/* Compatibility helpers for optional Samsung vendor services. */
#ifndef __SND_SOC_ABOX_COMPAT_H
#define __SND_SOC_ABOX_COMPAT_H

#include <linux/err.h>
#include <linux/notifier.h>
#include <linux/regmap.h>
#include <linux/soc/samsung/exynos-pmu.h>
#include <linux/types.h>

enum modem_event {
	MODEM_EVENT_RESET = 1,
	MODEM_EVENT_EXIT,
	MODEM_EVENT_ONLINE = 4,
	MODEM_EVENT_WATCHDOG = 9,
};

static inline int register_modem_event_notifier(struct notifier_block *nb)
{
	return 0;
}

struct itmon_notifier {
	char *dest;
};

static inline void itmon_notifier_chain_register(struct notifier_block *nb)
{
}

static inline void __iomem *shm_get_vss_region(void)
{
	return NULL;
}

static inline unsigned long shm_get_vss_base(void)
{
	return 0;
}

static inline u32 shm_get_vss_size(void)
{
	return 0;
}

static inline void __iomem *shm_get_vparam_region(void)
{
	return NULL;
}

static inline unsigned long shm_get_vparam_base(void)
{
	return 0;
}

static inline u32 shm_get_vparam_size(void)
{
	return 0;
}

static inline int exynos_pmu_read(unsigned int offset, unsigned int *value)
{
	struct regmap *map = exynos_get_pmu_regmap();

	if (IS_ERR(map))
		return PTR_ERR(map);

	return regmap_read(map, offset, value);
}

static inline int exynos_pmu_write(unsigned int offset, unsigned int value)
{
	struct regmap *map = exynos_get_pmu_regmap();

	if (IS_ERR(map))
		return PTR_ERR(map);

	return regmap_write(map, offset, value);
}

static inline int exynos_pmu_update(unsigned int offset, unsigned int mask,
		unsigned int value)
{
	struct regmap *map = exynos_get_pmu_regmap();

	if (IS_ERR(map))
		return PTR_ERR(map);

	return regmap_update_bits(map, offset, mask, value);
}

#endif /* __SND_SOC_ABOX_COMPAT_H */
