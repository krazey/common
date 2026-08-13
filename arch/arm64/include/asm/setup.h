// SPDX-License-Identifier: GPL-2.0

#ifndef __ARM64_ASM_SETUP_H
#define __ARM64_ASM_SETUP_H

#include <linux/string.h>

#include <uapi/asm/setup.h>

/*
 * These two variables are used in the head.S file.
 */
extern phys_addr_t __fdt_pointer __initdata;
extern u64 __cacheline_aligned boot_args[4];

#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
void exynos9810_early_boot_marker(u16 stage);
void exynos9810_early_cache_marker(const char *name, u16 stage);
void exynos9810_early_panic_log(const char *message);
void exynos9810_early_boot_marker_map(void);
void exynos9810_early_boot_marker_release(void);

#define exynos9810_boot_marker(first, second) \
	exynos9810_early_boot_marker((first) | ((second) << 8))

#define exynos9810_cache_marker(name, first, second) \
	exynos9810_early_cache_marker(name, (first) | ((second) << 8))
#endif

static inline bool arch_parse_debug_rodata(char *arg)
{
	extern bool rodata_enabled;
	extern bool rodata_full;

	if (!arg)
		return false;

	if (!strcmp(arg, "on")) {
		rodata_enabled = rodata_full = true;
		return true;
	}

	if (!strcmp(arg, "off")) {
		rodata_enabled = rodata_full = false;
		return true;
	}

	if (!strcmp(arg, "noalias")) {
		rodata_enabled = true;
		rodata_full = false;
		return true;
	}

	return false;
}
#define arch_parse_debug_rodata arch_parse_debug_rodata

#endif
