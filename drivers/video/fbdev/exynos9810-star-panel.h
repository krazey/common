/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __EXYNOS9810_STAR_PANEL_H__
#define __EXYNOS9810_STAR_PANEL_H__

#include <linux/types.h>

#define EXYNOS9810_STAR_NR_LUMINANCE	74
#define EXYNOS9810_STAR_MTP_LEN		34
#define EXYNOS9810_STAR_GAMMA_LEN	34
#define EXYNOS9810_STAR_MAX_BRIGHTNESS	255

struct exynos9810_star_panel {
	bool calibrated;
	u8 elvss_temp;
	u8 gamma[EXYNOS9810_STAR_NR_LUMINANCE]
		[EXYNOS9810_STAR_GAMMA_LEN];
};

struct exynos9810_star_setting {
	const u8 *gamma;
	const u8 *aor;
	const u8 *irc;
	u16 poc;
	u8 mps;
	u8 elvss;
};

int exynos9810_star_panel_calibrate(struct exynos9810_star_panel *panel,
				    const u8 *mtp, size_t mtp_len,
				    u8 elvss_temp);
int
exynos9810_star_panel_get_setting(const struct exynos9810_star_panel *panel,
				  unsigned int brightness,
				  struct exynos9810_star_setting *setting);

#endif /* __EXYNOS9810_STAR_PANEL_H__ */
