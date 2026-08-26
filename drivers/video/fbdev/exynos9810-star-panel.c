// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S6E3HA8 STAR_A3_S0 panel calibration
 *
 * The panel stores per-unit gamma offsets in MTP register 0xc8.  Generate
 * the normal 2-400 nit gamma payloads once, then keep the command path free
 * of the legacy fixed-point dimming calculations.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "exynos9810-panel-dimming.h"
#include "exynos9810-star-panel-dimming.h"
#include "exynos9810-star-panel.h"

#define EXYNOS9810_STAR_NR_TP	11

static struct tp exynos9810_star_tp[EXYNOS9810_STAR_NR_TP] = {
	{
		.level = 0, .volt_src = VREG_OUT, .name = "VT",
		.center = { 0x00, 0x00, 0x00 },
		.numerator = 0, .denominator = 860,
	}, {
		.level = 1, .volt_src = V0_OUT, .name = "V1",
		.center = { 0x80, 0x80, 0x80 },
		.numerator = 0, .denominator = 256,
	}, {
		.level = 7, .volt_src = V0_OUT, .name = "V7",
		.center = { 0x80, 0x80, 0x80 },
		.numerator = 64, .denominator = 320,
	}, {
		.level = 11, .volt_src = VT_OUT, .name = "V11",
		.center = { 0x80, 0x80, 0x80 },
		.numerator = 64, .denominator = 320,
	}, {
		.level = 23, .volt_src = VT_OUT, .name = "V23",
		.center = { 0x80, 0x80, 0x80 },
		.numerator = 64, .denominator = 320,
	}, {
		.level = 35, .volt_src = VT_OUT, .name = "V35",
		.center = { 0x80, 0x80, 0x80 },
		.numerator = 64, .denominator = 320,
	}, {
		.level = 51, .volt_src = VT_OUT, .name = "V51",
		.center = { 0x80, 0x80, 0x80 },
		.numerator = 64, .denominator = 320,
	}, {
		.level = 87, .volt_src = VT_OUT, .name = "V87",
		.center = { 0x80, 0x80, 0x80 },
		.numerator = 64, .denominator = 320,
	}, {
		.level = 151, .volt_src = VT_OUT, .name = "V151",
		.center = { 0x80, 0x80, 0x80 },
		.numerator = 64, .denominator = 320,
	}, {
		.level = 203, .volt_src = VT_OUT, .name = "V203",
		.center = { 0x80, 0x80, 0x80 },
		.numerator = 64, .denominator = 320,
	}, {
		.level = 255, .volt_src = VREG_OUT, .name = "V255",
		.center = { 0x100, 0x100, 0x100 },
		.numerator = 129, .denominator = 860,
	},
};

static const unsigned int exynos9810_star_brightness[] = {
	0, 7, 14, 21, 28, 35, 36, 38, 40, 42,
	44, 46, 48, 50, 52, 54, 56, 57, 59, 61,
	63, 65, 67, 69, 70, 72, 74, 76, 78, 80,
	82, 84, 86, 88, 90, 92, 94, 96, 98, 100,
	102, 104, 106, 108, 110, 112, 114, 116, 118, 120,
	122, 124, 126, 128, 135, 142, 149, 157, 165, 174,
	183, 193, 201, 210, 219, 223, 227, 230, 234, 238,
	242, 246, 250, 255,
};

static const unsigned int exynos9810_star_luminance[] = {
	2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
	12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
	23, 24, 26, 27, 29, 31, 33, 35, 37, 39,
	42, 45, 48, 51, 54, 57, 61, 65, 69, 73,
	78, 83, 88, 94, 100, 106, 113, 120, 128, 136,
	145, 154, 164, 174, 185, 197, 210, 223, 237, 253,
	269, 286, 301, 317, 333, 340, 347, 354, 362, 369,
	376, 384, 392, 400,
};

static const u8 exynos9810_star_aor[][2] = {
	{ 0x0b, 0x76 }, { 0x0b, 0x62 }, { 0x0b, 0x44 },
	{ 0x0b, 0x30 }, { 0x0b, 0x12 }, { 0x0a, 0xf7 },
	{ 0x0a, 0xdc }, { 0x0a, 0xbe }, { 0x0a, 0x9e },
	{ 0x0a, 0x7a }, { 0x0a, 0x52 }, { 0x0a, 0x2e },
	{ 0x0a, 0x01 }, { 0x09, 0xd2 }, { 0x09, 0xb2 },
	{ 0x09, 0x92 }, { 0x09, 0x72 }, { 0x09, 0x52 },
	{ 0x09, 0x34 }, { 0x09, 0x15 }, { 0x08, 0xd2 },
	{ 0x08, 0xb4 }, { 0x08, 0x72 }, { 0x08, 0x53 },
	{ 0x08, 0x0e }, { 0x07, 0xcd }, { 0x07, 0x8c },
	{ 0x07, 0x50 }, { 0x07, 0x0e }, { 0x06, 0xc0 },
	{ 0x06, 0x6c }, { 0x06, 0x12 }, { 0x05, 0x9e },
	{ 0x05, 0x44 }, { 0x04, 0xd0 }, { 0x04, 0x80 },
	{ 0x04, 0x80 }, { 0x04, 0x80 }, { 0x04, 0x80 },
	{ 0x04, 0x80 }, { 0x04, 0x80 }, { 0x04, 0x80 },
	{ 0x04, 0x80 }, { 0x04, 0x80 }, { 0x04, 0x80 },
	{ 0x04, 0x80 }, { 0x04, 0x80 }, { 0x04, 0x80 },
	{ 0x04, 0x80 }, { 0x04, 0x80 }, { 0x04, 0x80 },
	{ 0x04, 0x80 }, { 0x04, 0x80 }, { 0x04, 0x7e },
	{ 0x03, 0xfc }, { 0x03, 0x7a }, { 0x02, 0xe4 },
	{ 0x02, 0x30 }, { 0x01, 0x7c }, { 0x01, 0x7c },
	{ 0x01, 0x7c }, { 0x01, 0x7c }, { 0x01, 0x7c },
	{ 0x01, 0x7c }, { 0x01, 0x7c }, { 0x01, 0x7c },
	{ 0x01, 0x7a }, { 0x01, 0x3b }, { 0x00, 0xff },
	{ 0x00, 0xaf }, { 0x00, 0x37 }, { 0x00, 0x0c },
	{ 0x00, 0x0c }, { 0x00, 0x0c },
};

static const u8 exynos9810_star_elvss[] = {
	0x12, 0x12, 0x13, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x1a, 0x1c, 0x1e, 0x20, 0x22, 0x22, 0x22,
	0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
	0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
	0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
	0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
	0x21, 0x21, 0x20, 0x20, 0x1f, 0x1f, 0x1f, 0x1e,
	0x1e, 0x1e, 0x1e, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a,
	0x19, 0x19, 0x18, 0x18, 0x17, 0x17, 0x17, 0x17,
	0x16, 0x16,
};

/*
 * The stock IRC payload has eight fixed bytes followed by three repeated
 * RGB-neutral compensation values.  Store only those three values here.
 */
static const u8 exynos9810_star_irc[][3] = {
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },
	{ 0x02, 0x01, 0x00 }, { 0x02, 0x01, 0x00 },
	{ 0x02, 0x01, 0x01 }, { 0x02, 0x01, 0x01 },
	{ 0x03, 0x01, 0x00 }, { 0x03, 0x01, 0x01 },
	{ 0x03, 0x01, 0x01 }, { 0x03, 0x01, 0x01 },
	{ 0x03, 0x02, 0x01 }, { 0x04, 0x01, 0x01 },
	{ 0x04, 0x01, 0x01 }, { 0x04, 0x01, 0x02 },
	{ 0x04, 0x02, 0x01 }, { 0x05, 0x01, 0x02 },
	{ 0x05, 0x02, 0x01 }, { 0x05, 0x02, 0x02 },
	{ 0x06, 0x01, 0x02 }, { 0x06, 0x02, 0x02 },
	{ 0x06, 0x02, 0x02 }, { 0x07, 0x02, 0x02 },
	{ 0x07, 0x02, 0x03 }, { 0x07, 0x03, 0x02 },
	{ 0x08, 0x03, 0x02 }, { 0x09, 0x02, 0x03 },
	{ 0x09, 0x03, 0x03 }, { 0x0a, 0x03, 0x03 },
	{ 0x0a, 0x04, 0x03 }, { 0x0b, 0x04, 0x03 },
	{ 0x0c, 0x04, 0x03 }, { 0x0c, 0x05, 0x04 },
	{ 0x0d, 0x05, 0x04 }, { 0x0e, 0x05, 0x04 },
	{ 0x0f, 0x05, 0x05 }, { 0x10, 0x05, 0x05 },
	{ 0x11, 0x05, 0x06 }, { 0x12, 0x06, 0x06 },
	{ 0x13, 0x07, 0x06 }, { 0x14, 0x07, 0x07 },
	{ 0x15, 0x08, 0x07 }, { 0x17, 0x08, 0x07 },
	{ 0x18, 0x09, 0x08 }, { 0x1a, 0x09, 0x08 },
	{ 0x1c, 0x09, 0x09 }, { 0x1d, 0x0a, 0x0a },
	{ 0x1f, 0x0b, 0x0a }, { 0x21, 0x0b, 0x0b },
	{ 0x23, 0x0c, 0x0c }, { 0x25, 0x0d, 0x0d },
	{ 0x28, 0x0e, 0x0d }, { 0x2a, 0x0f, 0x0e },
	{ 0x2d, 0x0f, 0x0f }, { 0x30, 0x11, 0x0f },
	{ 0x33, 0x12, 0x10 }, { 0x36, 0x13, 0x12 },
	{ 0x39, 0x14, 0x13 }, { 0x3c, 0x15, 0x14 },
	{ 0x3f, 0x16, 0x15 }, { 0x41, 0x16, 0x15 },
	{ 0x42, 0x16, 0x16 }, { 0x43, 0x17, 0x16 },
	{ 0x45, 0x17, 0x17 }, { 0x46, 0x18, 0x17 },
	{ 0x47, 0x19, 0x17 }, { 0x49, 0x19, 0x18 },
	{ 0x4a, 0x1a, 0x18 }, { 0x4c, 0x1a, 0x19 },
};

static const u16 exynos9810_star_poc[] = {
	0x0c68, 0x0c68, 0x0c68, 0x0c68, 0x0c68, 0x0c68,
	0x0c68, 0x0c68, 0x0c68, 0x0c68, 0x0c68, 0x0c68,
	0x0c68, 0x0c68, 0x0c6a, 0x0c6c, 0x0c6e, 0x0c70,
	0x0c72, 0x0c74, 0x0c78, 0x0c7a, 0x0c7e, 0x0c80,
	0x0c84, 0x0c88, 0x0c8c, 0x0c90, 0x0c94, 0x0c98,
	0x0c9e, 0x0ca4, 0x0caa, 0x0cb0, 0x0cb6, 0x0cbc,
	0x0cc4, 0x0ccc, 0x0ce0, 0x0cf4, 0x0d0c, 0x0d25,
	0x0d3e, 0x0d5b, 0x0d78, 0x0d85, 0x0d95, 0x0da4,
	0x0db5, 0x0dc6, 0x0dda, 0x0ded, 0x0e03, 0x0e19,
	0x0e30, 0x0e47, 0x0e5f, 0x0e77, 0x0e91, 0x0eaf,
	0x0ecc, 0x0ef5, 0x0f18, 0x0f3e, 0x0f64, 0x0f74,
	0x0f84, 0x0f94, 0x0fa6, 0x0fb6, 0x0fc6, 0x0fd8,
	0x0fea, 0x0ffc,
};

static struct dimming_init_info exynos9810_star_dimming_init = {
	.name = "s6e3ha8-star-a3-s0",
	.nr_tp = EXYNOS9810_STAR_NR_TP,
	.tp = exynos9810_star_tp,
	.nr_luminance = EXYNOS9810_STAR_NR_LUMINANCE,
	.vregout = 114085069LL,
	.bitshift = 24,
	.vt_voltage = {
		0, 12, 24, 36, 48, 60, 72, 84,
		96, 108, 138, 148, 158, 168, 178, 186,
	},
	.target_luminance = 400,
	.target_gamma = 220,
	.dim_lut = star_a3_s0_dimming_lut,
};

static void
exynos9810_star_mtp_to_int(s32 dst[][MAX_COLOR], const u8 *src)
{
	int color;
	int point;
	int position = 1;
	int sign;

	for (point = EXYNOS9810_STAR_NR_TP - 1; point > 0; point--) {
		for (color = 0; color < MAX_COLOR; color++) {
			if (point == EXYNOS9810_STAR_NR_TP - 1) {
				sign = src[0] & BIT(0) ? -1 : 1;
				dst[point][color] = sign * src[position++];
			} else {
				sign = src[position] & BIT(7) ? -1 : 1;
				dst[point][color] =
					sign * (src[position++] & GENMASK(6, 0));
			}
		}
	}

	dst[0][RED] = src[position + 1] & GENMASK(3, 0);
	dst[0][GREEN] = (src[position + 2] >> 4) & GENMASK(3, 0);
	dst[0][BLUE] = src[position + 2] & GENMASK(3, 0);
}

static void exynos9810_star_copy_gamma(u8 *output, u32 value, u32 point,
				       u32 color)
{
	int index = EXYNOS9810_STAR_NR_TP - point - 1;

	if (point == EXYNOS9810_STAR_NR_TP - 1) {
		output[0] &= ~BIT(color);
		output[0] |= !!(value & BIT(8)) << color;
		output[color + 1] = value;
	} else if (!point) {
		if (color == RED) {
			output[index * MAX_COLOR + 1] &= ~GENMASK(7, 4);
			output[index * MAX_COLOR + 2] &= ~GENMASK(3, 0);
			output[index * MAX_COLOR + 2] |=
				value & GENMASK(3, 0);
		} else if (color == GREEN) {
			output[index * MAX_COLOR + 1] &= ~GENMASK(3, 0);
			output[index * MAX_COLOR + 3] &= ~GENMASK(7, 4);
			output[index * MAX_COLOR + 3] |=
				(value & GENMASK(3, 0)) << 4;
		} else {
			output[index * MAX_COLOR + 2] &= ~GENMASK(7, 4);
			output[index * MAX_COLOR + 2] |=
				(value & GENMASK(3, 0)) << 4;
			output[index * MAX_COLOR + 3] &= ~GENMASK(3, 0);
			output[index * MAX_COLOR + 3] |=
				value & GENMASK(3, 0);
		}
	} else {
		output[index * MAX_COLOR + color + 1] = value;
	}
}

int exynos9810_star_panel_calibrate(struct exynos9810_star_panel *panel,
				    const u8 *mtp, size_t mtp_len,
				    u8 elvss_temp)
{
	s32 mtp_offset[EXYNOS9810_STAR_NR_TP][MAX_COLOR] = { };
	struct dimming_info *dimming;
	unsigned int index;
	int ret;

	if (!panel || !mtp || mtp_len != EXYNOS9810_STAR_MTP_LEN)
		return -EINVAL;
	if (panel->calibrated)
		return 0;

	static_assert(ARRAY_SIZE(exynos9810_star_brightness) ==
		      EXYNOS9810_STAR_NR_LUMINANCE);
	static_assert(ARRAY_SIZE(exynos9810_star_luminance) ==
		      EXYNOS9810_STAR_NR_LUMINANCE);
	static_assert(ARRAY_SIZE(exynos9810_star_aor) ==
		      EXYNOS9810_STAR_NR_LUMINANCE);
	static_assert(ARRAY_SIZE(exynos9810_star_elvss) ==
		      EXYNOS9810_STAR_NR_LUMINANCE);
	static_assert(ARRAY_SIZE(exynos9810_star_irc) ==
		      EXYNOS9810_STAR_NR_LUMINANCE);
	static_assert(ARRAY_SIZE(exynos9810_star_poc) ==
		      EXYNOS9810_STAR_NR_LUMINANCE);

	dimming = kzalloc_obj(*dimming);
	if (!dimming)
		return -ENOMEM;

	exynos9810_star_mtp_to_int(mtp_offset, mtp);
	ret = init_dimming_info(dimming, &exynos9810_star_dimming_init);
	if (ret)
		goto free_dimming;

	ret = init_dimming_mtp(dimming, mtp_offset);
	if (ret)
		goto free_dimming;
	ret = process_dimming(dimming);
	if (ret)
		goto free_dimming;

	for (index = 0; index < EXYNOS9810_STAR_NR_LUMINANCE; index++) {
		memset(panel->gamma[index], 0,
		       sizeof(panel->gamma[index]));
		get_dimming_gamma(dimming, exynos9810_star_luminance[index],
				  panel->gamma[index],
				  exynos9810_star_copy_gamma);
	}

	panel->elvss_temp = elvss_temp;
	panel->calibrated = true;
free_dimming:
	kfree(dimming);
	return ret;
}

int
exynos9810_star_panel_get_setting(const struct exynos9810_star_panel *panel,
				  unsigned int brightness,
				  struct exynos9810_star_setting *setting)
{
	unsigned int index;

	if (!panel || !panel->calibrated || !setting)
		return -ENODEV;

	brightness = min_t(unsigned int, brightness,
			   EXYNOS9810_STAR_MAX_BRIGHTNESS);
	for (index = 0; index < EXYNOS9810_STAR_NR_LUMINANCE - 1; index++)
		if (brightness <= exynos9810_star_brightness[index])
			break;

	setting->gamma = panel->gamma[index];
	setting->aor = exynos9810_star_aor[index];
	setting->irc = exynos9810_star_irc[index];
	setting->poc = exynos9810_star_poc[index];
	setting->mps = exynos9810_star_luminance[index] <= 39 ? 0xcc : 0xdc;
	setting->elvss = exynos9810_star_elvss[index];
	return 0;
}
