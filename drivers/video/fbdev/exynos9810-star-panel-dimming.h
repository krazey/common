/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * S6E3HA8 STAR_A3_S0 panel dimming data
 *
 * Header file for S6E3HA8 Dimming Driver
 *
 * Copyright (c) 2016 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __EXYNOS9810_STAR_PANEL_DIMMING_H__
#define __EXYNOS9810_STAR_PANEL_DIMMING_H__

#include "exynos9810-panel-dimming.h"

/*
 * PANEL INFORMATION
 * LDI : S6E3HA8
 * PANEL : STAR_A3_S0
 */
/* gray scale offset values */
static s32 star_a3_s0_rtbl2nit[11] = { 0, 0, 29, 26, 22, 18, 15, 11, 6, 3, 0 };
static s32 star_a3_s0_rtbl3nit[11] = { 0, 0, 22, 20, 17, 14, 11, 9, 6, 5, 0 };
static s32 star_a3_s0_rtbl4nit[11] = { 0, 0, 20, 18, 15, 12, 10, 7, 4, 3, 0 };
static s32 star_a3_s0_rtbl5nit[11] = { 0, 0, 18, 16, 13, 10, 8, 6, 4, 3, 0 };
static s32 star_a3_s0_rtbl6nit[11] = { 0, 0, 17, 15, 12, 9, 7, 4, 3, 3, 0 };
static s32 star_a3_s0_rtbl7nit[11] = { 0, 0, 16, 15, 11, 9, 7, 4, 3, 2, 0 };
static s32 star_a3_s0_rtbl8nit[11] = { 0, 0, 16, 14, 11, 8, 7, 4, 2, 2, 0 };
static s32 star_a3_s0_rtbl9nit[11] = { 0, 0, 16, 14, 10, 8, 7, 4, 2, 2, 0 };
static s32 star_a3_s0_rtbl10nit[11] = { 0, 0, 15, 13, 10, 8, 6, 4, 2, 2, 0 };
static s32 star_a3_s0_rtbl11nit[11] = { 0, 0, 16, 14, 11, 8, 7, 4, 2, 2, 0 };
static s32 star_a3_s0_rtbl12nit[11] = { 0, 0, 16, 14, 11, 9, 7, 4, 3, 3, 0 };
static s32 star_a3_s0_rtbl13nit[11] = { 0, 0, 17, 15, 12, 9, 8, 4, 3, 3, 0 };
static s32 star_a3_s0_rtbl14nit[11] = { 0, 0, 17, 15, 12, 10, 8, 5, 3, 3, 0 };
static s32 star_a3_s0_rtbl15nit[11] = { 0, 0, 17, 15, 13, 10, 8, 6, 3, 3, 0 };
static s32 star_a3_s0_rtbl16nit[11] = { 0, 0, 17, 15, 12, 10, 8, 5, 3, 2, 0 };
static s32 star_a3_s0_rtbl17nit[11] = { 0, 0, 16, 14, 11, 9, 7, 4, 3, 2, 0 };
static s32 star_a3_s0_rtbl18nit[11] = { 0, 0, 15, 14, 10, 9, 7, 4, 3, 2, 0 };
static s32 star_a3_s0_rtbl19nit[11] = { 0, 0, 15, 13, 10, 8, 6, 4, 3, 2, 0 };
static s32 star_a3_s0_rtbl20nit[11] = { 0, 0, 15, 13, 10, 8, 6, 4, 3, 2, 0 };
static s32 star_a3_s0_rtbl21nit[11] = { 0, 0, 14, 12, 9, 8, 6, 4, 2, 2, 0 };
static s32 star_a3_s0_rtbl23nit[11] = { 0, 0, 13, 11, 9, 7, 5, 3, 2, 2, 0 };
static s32 star_a3_s0_rtbl24nit[11] = { 0, 0, 13, 11, 8, 7, 5, 3, 2, 2, 0 };
static s32 star_a3_s0_rtbl26nit[11] = { 0, 0, 12, 10, 8, 6, 5, 3, 2, 2, 0 };
static s32 star_a3_s0_rtbl27nit[11] = { 0, 0, 12, 10, 8, 6, 5, 3, 2, 2, 0 };
static s32 star_a3_s0_rtbl29nit[11] = { 0, 0, 11, 9, 7, 5, 4, 2, 2, 2, 0 };
static s32 star_a3_s0_rtbl31nit[11] = { 0, 0, 10, 9, 7, 5, 4, 2, 2, 1, 0 };
static s32 star_a3_s0_rtbl33nit[11] = { 0, 0, 10, 8, 6, 4, 4, 2, 2, 1, 0 };
static s32 star_a3_s0_rtbl35nit[11] = { 0, 0, 10, 8, 6, 4, 4, 2, 2, 1, 0 };
static s32 star_a3_s0_rtbl37nit[11] = { 0, 0, 9, 7, 6, 4, 3, 2, 2, 1, 0 };
static s32 star_a3_s0_rtbl39nit[11] = { 0, 0, 9, 7, 5, 3, 3, 1, 1, 1, 0 };
static s32 star_a3_s0_rtbl42nit[11] = { 0, 0, 8, 6, 5, 3, 3, 1, 2, 1, 0 };
static s32 star_a3_s0_rtbl45nit[11] = { 0, 0, 8, 6, 5, 3, 3, 1, 2, 2, 0 };
static s32 star_a3_s0_rtbl48nit[11] = { 0, 0, 7, 5, 4, 2, 2, 1, 1, 3, 0 };
static s32 star_a3_s0_rtbl51nit[11] = { 0, 0, 7, 5, 4, 2, 2, 1, 2, 2, 0 };
static s32 star_a3_s0_rtbl54nit[11] = { 0, 0, 6, 4, 4, 2, 2, 1, 1, 3, 0 };
static s32 star_a3_s0_rtbl57nit[11] = { 0, 0, 6, 4, 3, 2, 2, 2, 1, 3, 0 };
static s32 star_a3_s0_rtbl61nit[11] = { 0, 0, 6, 3, 3, 1, 2, 1, 1, 3, 0 };
static s32 star_a3_s0_rtbl65nit[11] = { 0, 0, 5, 3, 3, 2, 2, 0, 0, 0, 0 };
static s32 star_a3_s0_rtbl69nit[11] = { 0, 0, 6, 3, 3, 1, 2, 1, 2, 1, 0 };
static s32 star_a3_s0_rtbl73nit[11] = { 0, 0, 5, 4, 3, 2, 2, 2, 2, 1, 0 };
static s32 star_a3_s0_rtbl78nit[11] = { 0, 0, 5, 4, 3, 2, 2, 1, 2, 2, 0 };
static s32 star_a3_s0_rtbl83nit[11] = { 0, 0, 6, 3, 2, 1, 2, 3, 2, 1, 0 };
static s32 star_a3_s0_rtbl88nit[11] = { 0, 0, 6, 3, 3, 2, 2, 1, 2, 1, 0 };
static s32 star_a3_s0_rtbl94nit[11] = { 0, 0, 5, 3, 2, 2, 2, 2, 2, 0, 0 };
static s32 star_a3_s0_rtbl100nit[11] = { 0, 0, 6, 4, 3, 2, 2, 2, 2, -2, 0 };
static s32 star_a3_s0_rtbl106nit[11] = { 0, 0, 5, 3, 2, 1, 1, 1, 1, -3, 0 };
static s32 star_a3_s0_rtbl113nit[11] = { 0, 0, 5, 3, 3, 2, 2, 2, 1, -1, 0 };
static s32 star_a3_s0_rtbl120nit[11] = { 0, 0, 5, 3, 3, 2, 2, 2, 2, -1, 0 };
static s32 star_a3_s0_rtbl128nit[11] = { 0, 0, 5, 3, 2, 2, 2, 2, 2, 0, 0 };
static s32 star_a3_s0_rtbl136nit[11] = { 0, 0, 5, 3, 2, 2, 2, 2, 2, -1, 0 };
static s32 star_a3_s0_rtbl145nit[11] = { 0, 0, 5, 2, 1, 1, 1, 2, 2, 0, 0 };
static s32 star_a3_s0_rtbl154nit[11] = { 0, 0, 5, 2, 2, 2, 2, 2, 2, 1, 0 };
static s32 star_a3_s0_rtbl164nit[11] = { 0, 0, 4, 2, 1, 2, 1, 2, 3, 1, 0 };
static s32 star_a3_s0_rtbl174nit[11] = { 0, 0, 4, 3, 2, 1, 1, 1, 2, -1, -1 };
static s32 star_a3_s0_rtbl185nit[11] = { 0, 0, 3, 2, 1, 1, 1, 1, 2, -1, -1 };
static s32 star_a3_s0_rtbl197nit[11] = { 0, 0, 2, 2, 1, 1, 1, 1, 1, -1, -1 };
static s32 star_a3_s0_rtbl210nit[11] = { 0, 0, 1, 1, 1, 0, 0, 1, 1, -1, -1 };
static s32 star_a3_s0_rtbl223nit[11] = { 0, 0, 1, 1, 1, 0, 0, 0, 1, -1, 0 };
static s32 star_a3_s0_rtbl237nit[11] = { 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0 };
static s32 star_a3_s0_rtbl253nit[11] = { 0, 0, 0, 1, 0, 0, 0, 0, 1, -1, 0 };
static s32 star_a3_s0_rtbl269nit[11] = { 0, 0, 0, -1, 0, 0, 0, -1, 0, -1, 0 };
static s32 star_a3_s0_rtbl286nit[11] = { 0, 0, 0, -1, -1, -1, -1, -1, 0, -4, 0 };
static s32 star_a3_s0_rtbl301nit[11] = { 0, 0, 0, 0, 0, 0, -1, 0, 1, 2, 0 };
static s32 star_a3_s0_rtbl317nit[11] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0 };
static s32 star_a3_s0_rtbl333nit[11] = { 0, 0, 0, 0, -1, -1, -1, -1, -1, 3, 0 };
static s32 star_a3_s0_rtbl340nit[11] = { 0, 0, 0, -1, -1, -1, -1, -1, -1, 1, 0 };
static s32 star_a3_s0_rtbl347nit[11] = { 0, 0, 0, -1, -1, -1, -1, -1, 0, 1, 0 };
static s32 star_a3_s0_rtbl354nit[11] = { 0, 0, 0, -2, -1, -1, -1, -1, 0, 1, 0 };
static s32 star_a3_s0_rtbl362nit[11] = { 0, 0, 0, -2, -1, -1, -1, -2, -1, 1, 0 };
static s32 star_a3_s0_rtbl369nit[11] = { 0, 0, 0, -2, -1, -1, -1, -2, -1, 1, 0 };
static s32 star_a3_s0_rtbl376nit[11] = { 0, 0, 1, -1, -1, -1, -1, -2, -3, -1, 0 };
static s32 star_a3_s0_rtbl384nit[11] = { 0, 0, 0, -1, -1, -1, -1, -2, -3, -1, 0 };
static s32 star_a3_s0_rtbl392nit[11] = { 0, 0, -1, -1, -1, -1, -1, -2, -3, 0, 0 };
static s32 star_a3_s0_rtbl400nit[11] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* rgb color offset values */
static s32 star_a3_s0_ctbl2nit[33] = { 0, 0, 0, 0, 0, 0, -4, 0, -1, -21, 0, -14, -15, -1, -12, -11, 0, -9, -9, 1, -9, -4, 0, -4, -2, -1, -3, 0, 0, 0, -2, 0, -1 };
static s32 star_a3_s0_ctbl3nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -1, -11, 0, -13, -9, 1, -10, -11, -2, -9, -6, 1, -6, -4, -1, -4, -1, 0, -2, 0, 0, 0, -2, 0, -1 };
static s32 star_a3_s0_ctbl4nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -1, -9, -1, -14, -10, 1, -9, -5, 2, -6, -5, 0, -7, -4, 0, -4, -1, -1, -2, 1, 0, 1, -2, 0, -1 };
static s32 star_a3_s0_ctbl5nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -8, -6, 0, -13, -11, -2, -11, -5, 0, -6, -4, 1, -5, -4, 0, -3, -1, -1, -2, 0, 0, 0, 0, 0, 0 };
static s32 star_a3_s0_ctbl6nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -9, -5, 1, -11, -9, -3, -11, -3, 0, -6, -6, 1, -6, -2, 1, -2, 0, 0, -1, -1, 0, -1, 0, 0, 1 };
static s32 star_a3_s0_ctbl7nit[33] = { 0, 0, 0, 0, 0, 0, 4, 4, -4, -7, -4, -16, -6, 2, -7, -2, -1, -6, -6, 0, -7, -3, 1, -3, 1, 0, 1, -1, 0, -1, 0, 0, 1 };
static s32 star_a3_s0_ctbl8nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -10, 0, 3, -10, -8, -3, -11, 0, 2, -3, -6, 0, -7, -3, -1, -3, 1, 0, 0, 0, 0, 0, 0, 0, 1 };
static s32 star_a3_s0_ctbl9nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -9, -4, -2, -17, -5, 0, -8, 0, 2, -3, -5, 0, -7, -3, -1, -3, 0, 0, 0, 0, 0, 0, 1, 0, 1 };
static s32 star_a3_s0_ctbl10nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -1, 4, 2, -9, -4, 0, -8, -2, -1, -7, -4, 1, -6, -3, -1, -3, 1, 0, 0, -1, 0, 0, 2, 1, 2 };
static s32 star_a3_s0_ctbl11nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -11, 3, 0, -13, -5, -3, -11, 1, 2, -4, -5, 0, -7, -3, -1, -3, 1, 0, 0, -1, 0, 0, 2, 1, 2 };
static s32 star_a3_s0_ctbl12nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -1, 4, 0, -11, -1, 1, -8, -1, -2, -7, -5, 1, -7, -2, 1, -2, 1, 0, -1, -1, 0, 0, 2, 1, 2 };
static s32 star_a3_s0_ctbl13nit[33] = { 0, 0, 0, 0, 0, 0, -1, 2, -12, 4, -1, -12, -4, -3, -12, 1, 2, -4, -6, -1, -9, -1, 1, -1, 1, 0, -1, -1, 0, 0, 2, 1, 2 };
static s32 star_a3_s0_ctbl14nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -12, 5, -1, -12, 1, 1, -8, -1, -1, -7, -6, 0, -9, -2, 0, -2, 0, 0, -1, 0, 0, 0, 2, 1, 3 };
static s32 star_a3_s0_ctbl15nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -12, 9, 4, -8, -4, -3, -13, 0, 0, -6, -3, 1, -8, -3, -1, -3, 0, 0, -1, 0, 0, 0, 3, 1, 3 };
static s32 star_a3_s0_ctbl16nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -13, 5, -1, -11, 0, 0, -10, -1, -1, -7, -5, 0, -8, -3, 0, -2, 0, 0, 0, 0, 0, 0, 3, 1, 3 };
static s32 star_a3_s0_ctbl17nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -13, 6, 1, -11, 0, -1, -10, -2, -1, -7, -4, 1, -7, -2, 1, -2, 0, 0, 0, 1, 0, 1, 3, 1, 3 };
static s32 star_a3_s0_ctbl18nit[33] = { 0, 0, 0, 0, 0, 0, 4, 4, -6, 1, -4, -16, 3, 3, -8, -2, -1, -7, -5, -1, -8, -1, 1, -2, -1, 0, 0, 1, 0, 1, 3, 1, 3 };
static s32 star_a3_s0_ctbl19nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -1, 7, 1, -9, -2, -2, -12, -1, 0, -5, -3, 1, -6, -2, 0, -3, -1, 0, 0, 1, 0, 1, 3, 1, 3 };
static s32 star_a3_s0_ctbl20nit[33] = { 0, 0, 0, 0, 0, 0, -1, 2, -12, 6, -1, -11, -3, -2, -12, -1, -1, -6, -4, 1, -6, -1, 0, -2, -1, 0, 0, 1, 0, 1, 3, 1, 3 };
static s32 star_a3_s0_ctbl21nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -14, 5, 0, -11, 3, 4, -7, -2, -2, -6, -5, 0, -7, -1, -1, -2, 0, 0, 0, 1, 0, 1, 3, 0, 3 };
static s32 star_a3_s0_ctbl23nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -14, 9, 4, -7, -2, -2, -11, -3, -1, -5, -3, 1, -7, -1, 0, -1, 1, 0, 0, 0, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl24nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -14, 5, -2, -11, 2, 4, -6, -3, -2, -6, -3, 1, -6, -1, 0, -1, 1, 0, 0, 0, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl26nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -14, 9, 3, -8, -3, -1, -10, 1, 1, -3, -3, 0, -6, -3, -1, -3, 2, 0, 1, 0, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl27nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -14, 8, 3, -8, -3, -2, -10, 0, 0, -4, -3, 0, -6, -3, -1, -3, 2, 0, 1, 0, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl29nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -14, 8, 1, -8, -2, -1, -9, -1, 1, -4, -3, 0, -5, 0, 0, 0, 1, -1, 0, 0, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl31nit[33] = { 0, 0, 0, 0, 0, 0, 5, 5, -8, 6, 0, -10, -3, -2, -9, -1, 0, -4, -3, 0, -5, -1, -1, -1, 1, -1, 0, 0, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl33nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -14, 6, 0, -10, -1, 0, -7, 1, 2, -2, -3, 0, -4, -1, -1, -1, 1, -1, 0, 0, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl35nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -13, 5, -1, -11, -1, 0, -7, 1, 2, -1, -4, -1, -5, -1, -1, -1, 1, -1, 0, 0, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl37nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -13, 8, 4, -8, -1, -1, -7, -2, -2, -4, -1, 2, -2, -1, -1, -1, 0, -1, -1, 1, 0, 1, 3, 0, 4 };
static s32 star_a3_s0_ctbl39nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -13, 3, -3, -13, -1, 0, -5, 1, 2, -2, -3, -1, -4, 0, 0, 0, 0, 0, -1, 1, 0, 1, 3, 0, 4 };
static s32 star_a3_s0_ctbl42nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -11, 6, 2, -10, -2, -1, -5, 2, 2, -1, -4, -2, -5, 0, 1, 1, 0, -1, -1, 1, 0, 1, 3, 0, 4 };
static s32 star_a3_s0_ctbl45nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -11, 7, 1, -9, -3, -2, -6, 1, 1, -1, -4, -2, -5, 0, 1, 1, 0, -1, -1, 1, 0, 1, 3, 0, 4 };
static s32 star_a3_s0_ctbl48nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -11, 6, 0, -10, -3, -1, -6, 2, 1, -1, -1, 2, -1, -1, -1, -1, 0, 0, -1, 1, 0, 1, 3, 0, 4 };
static s32 star_a3_s0_ctbl51nit[33] = { 0, 0, 0, 0, 0, 0, -1, 0, -10, 5, 1, -10, -5, -3, -7, 3, 1, 0, -2, 1, -3, 0, 1, 2, 0, -1, -1, 1, 0, 1, 3, 0, 4 };
static s32 star_a3_s0_ctbl54nit[33] = { 0, 0, 0, 0, 0, 0, -1, -3, -14, 9, 4, -6, -4, -3, -6, 2, 0, -1, -2, 1, -3, -1, -1, 0, 0, 0, -1, 1, 0, 1, 3, 0, 4 };
static s32 star_a3_s0_ctbl57nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -10, 2, -3, -12, 1, 3, -1, 2, -1, -1, -2, 1, -3, -1, -1, 0, 1, 0, 0, 0, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl61nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -9, 6, 2, -8, -5, -3, -6, 4, 2, 1, -2, 0, -2, 0, 0, 0, 0, -1, -1, 0, 0, 1, 4, 1, 4 };
static s32 star_a3_s0_ctbl65nit[33] = { 0, 0, 0, 0, 0, 0, 6, 0, -7, 4, 1, -9, 0, 1, -2, 2, 0, 0, -3, -2, -5, -1, 0, 0, 1, 0, 1, 1, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl69nit[33] = { 0, 0, 0, 0, 0, 0, -1, -2, -10, 4, 0, -9, -1, 0, -3, 2, 1, 0, -1, 0, -2, -1, 0, 0, 0, 0, 0, 1, 0, 1, 3, 1, 4 };
static s32 star_a3_s0_ctbl73nit[33] = { 0, 0, 0, 0, 0, 0, 6, 0, -6, 1, 0, -11, -4, -3, -5, 2, 1, 0, -1, 0, -2, -1, 0, 0, 0, -1, 0, 1, 0, 1, 3, 1, 3 };
static s32 star_a3_s0_ctbl78nit[33] = { 0, 0, 0, 0, 0, 0, 6, 2, -2, -1, -2, -14, 1, 1, -1, 1, 0, -1, -3, -1, -4, 0, 0, 1, 0, 0, 0, 0, 0, 1, 3, 1, 4 };
static s32 star_a3_s0_ctbl83nit[33] = { 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -2, -12, 1, 1, -1, 3, 2, 2, -2, 0, -3, -1, -1, 0, 0, 0, -1, 1, 0, 1, 3, 1, 3 };
static s32 star_a3_s0_ctbl88nit[33] = { 0, 0, 0, 0, 0, 0, -1, -3, -10, 3, 3, -7, -2, -1, -4, 2, 1, 0, -3, -2, -4, 1, 1, 2, 0, -1, 0, 0, 0, 0, 4, 1, 4 };
static s32 star_a3_s0_ctbl94nit[33] = { 0, 0, 0, 0, 0, 0, 5, -1, -6, 3, 2, -7, 1, 3, -1, 1, -1, -1, -1, 0, -2, -1, -1, -1, 0, 0, 1, 1, 1, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl100nit[33] = { 0, 0, 0, 0, 0, 0, 3, 1, -7, 1, 0, -9, -1, 0, -3, 0, -1, -1, -1, 0, -2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl106nit[33] = { 0, 0, 0, 0, 0, 0, 3, 0, -8, 0, -1, -9, -1, 0, -3, 2, 2, 1, -2, -1, -3, 1, 1, 1, 0, -1, 0, 1, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl113nit[33] = { 0, 0, 0, 0, 0, 0, 2, 0, -8, 4, 2, -5, -3, -3, -5, 0, 0, -1, -2, 0, -2, 0, 0, 0, 1, 0, 1, -1, 0, 0, 4, 1, 4 };
static s32 star_a3_s0_ctbl120nit[33] = { 0, 0, 0, 0, 0, 0, 2, -1, -9, 3, 2, -5, 1, 1, -1, -2, -1, -3, 0, 1, -1, 0, -1, 1, 0, 0, 0, -1, 0, 0, 4, 0, 4 };
static s32 star_a3_s0_ctbl128nit[33] = { 0, 0, 0, 0, 0, 0, 0, -1, -10, 1, 0, -6, -1, 0, -2, 1, 1, 0, -2, -1, -3, 1, 1, 2, 0, 0, 0, -1, 0, 0, 3, 0, 3 };
static s32 star_a3_s0_ctbl136nit[33] = { 0, 0, 0, 0, 0, 0, 0, -2, -10, 0, 0, -6, 3, 2, 1, 0, 0, -1, -1, 0, -1, 0, -1, 0, 1, 0, 0, -1, 0, 0, 4, 1, 4 };
static s32 star_a3_s0_ctbl145nit[33] = { 0, 0, 0, 0, 0, 0, -1, -2, -10, -2, -2, -7, 1, 1, -1, 0, 0, -1, 0, 0, 0, 1, 0, 2, 0, 0, 0, -1, 0, 0, 3, 1, 4 };
static s32 star_a3_s0_ctbl154nit[33] = { 0, 0, 0, 0, 0, 0, -1, -3, -11, 3, 2, -2, -1, -1, -2, 1, 2, 0, -1, -1, -2, 0, 0, 0, 1, 1, 0, -1, 0, 0, 3, 0, 4 };
static s32 star_a3_s0_ctbl164nit[33] = { 0, 0, 0, 0, 0, 0, 4, 0, -5, 1, 1, -3, 2, 2, 0, -3, -1, -3, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 1, 3 };
static s32 star_a3_s0_ctbl174nit[33] = { 0, 0, 0, 0, 0, 0, 2, 1, -7, -2, -1, -5, -2, -3, -4, -2, 0, -2, 2, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 1 };
static s32 star_a3_s0_ctbl185nit[33] = { 0, 0, 0, 0, 0, 0, 2, -2, -6, 0, 0, -3, 2, 1, 0, -2, 0, -2, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, -1, 1 };
static s32 star_a3_s0_ctbl197nit[33] = { 0, 0, 0, 0, 0, 0, 9, 2, 1, -2, -2, -5, 2, 1, 1, -3, -1, -3, 1, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, -1, 1 };
static s32 star_a3_s0_ctbl210nit[33] = { 0, 0, 0, 0, 0, 0, 9, 0, 0, 2, 3, -1, -1, -3, -2, -1, 0, -1, 2, 2, 1, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, -1, 1 };
static s32 star_a3_s0_ctbl223nit[33] = { 0, 0, 0, 0, 0, 0, 9, -1, -1, 1, 2, -1, -2, -3, -3, -1, -1, -1, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 0, 2 };
static s32 star_a3_s0_ctbl237nit[33] = { 0, 0, 0, 0, 0, 0, 7, -1, 0, 4, 1, 1, 2, 2, 1, -1, 0, -1, 1, 0, 1, -1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 2 };
static s32 star_a3_s0_ctbl253nit[33] = { 0, 0, 0, 0, 0, 0, 12, 1, 4, 1, 0, -1, -3, -4, -4, 1, 1, 1, 0, -1, -1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 2, 1, 3 };
static s32 star_a3_s0_ctbl269nit[33] = { 0, 0, 0, 0, 0, 0, 4, -3, -2, 5, 3, 3, 1, 0, 0, -1, 1, -1, 0, -2, -1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 0, 2 };
static s32 star_a3_s0_ctbl286nit[33] = { 0, 0, 0, 0, 0, 0, 3, -4, -2, 5, 1, 2, -1, 0, -1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1 };
static s32 star_a3_s0_ctbl301nit[33] = { 0, 0, 0, 0, 0, 0, 9, 0, 4, 3, 1, 1, -2, -2, -2, -1, 0, -1, 1, 1, 1, 0, 0, 1, 1, 0, 0, -1, 0, 0, 2, 1, 2 };
static s32 star_a3_s0_ctbl317nit[33] = { 0, 0, 0, 0, 0, 0, 8, -2, 3, 2, 0, 1, 1, 0, 0, -3, -1, -2, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2 };
static s32 star_a3_s0_ctbl333nit[33] = { 0, 0, 0, 0, 0, 0, 7, -2, 2, 1, -1, 1, 1, 0, 0, -3, -1, -2, 2, 1, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 2 };
static s32 star_a3_s0_ctbl340nit[33] = { 0, 0, 0, 0, 0, 0, 7, -2, 2, 1, -2, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2 };
static s32 star_a3_s0_ctbl347nit[33] = { 0, 0, 0, 0, 0, 0, 5, -4, 1, 1, -2, 0, 0, 0, 0, -1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1 };
static s32 star_a3_s0_ctbl354nit[33] = { 0, 0, 0, 0, 0, 0, -2, -8, -5, 7, 2, 7, 0, -1, -1, -1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1 };
static s32 star_a3_s0_ctbl362nit[33] = { 0, 0, 0, 0, 0, 0, -2, -8, -5, 6, 1, 6, -1, -1, -1, -1, 1, 0, 0, -1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1 };
static s32 star_a3_s0_ctbl369nit[33] = { 0, 0, 0, 0, 0, 0, -3, -7, -5, 5, -1, 5, 1, 0, 0, -2, 0, -1, 0, -1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1 };
static s32 star_a3_s0_ctbl376nit[33] = { 0, 0, 0, 0, 0, 0, -4, -3, -5, 1, 2, 1, 0, 0, -1, -1, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, -1, 1, 0, 1, -1, 0, 0 };
static s32 star_a3_s0_ctbl384nit[33] = { 0, 0, 0, 0, 0, 0, 2, 1, 1, 1, 1, 1, 1, 0, 0, -2, 1, -1, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1 };
static s32 star_a3_s0_ctbl392nit[33] = { 0, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 1, 0, 0, -2, 0, -1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1 };
static s32 star_a3_s0_ctbl400nit[33] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static struct dimming_lut star_a3_s0_dimming_lut[] = {
	DIM_LUT_V0_INIT(2, 105, GAMMA_2_15, star_a3_s0_rtbl2nit, star_a3_s0_ctbl2nit),
	DIM_LUT_V0_INIT(3, 105, GAMMA_2_15, star_a3_s0_rtbl3nit, star_a3_s0_ctbl3nit),
	DIM_LUT_V0_INIT(4, 105, GAMMA_2_15, star_a3_s0_rtbl4nit, star_a3_s0_ctbl4nit),
	DIM_LUT_V0_INIT(5, 105, GAMMA_2_15, star_a3_s0_rtbl5nit, star_a3_s0_ctbl5nit),
	DIM_LUT_V0_INIT(6, 105, GAMMA_2_15, star_a3_s0_rtbl6nit, star_a3_s0_ctbl6nit),
	DIM_LUT_V0_INIT(7, 105, GAMMA_2_15, star_a3_s0_rtbl7nit, star_a3_s0_ctbl7nit),
	DIM_LUT_V0_INIT(8, 105, GAMMA_2_15, star_a3_s0_rtbl8nit, star_a3_s0_ctbl8nit),
	DIM_LUT_V0_INIT(9, 105, GAMMA_2_15, star_a3_s0_rtbl9nit, star_a3_s0_ctbl9nit),
	DIM_LUT_V0_INIT(10, 105, GAMMA_2_15, star_a3_s0_rtbl10nit, star_a3_s0_ctbl10nit),
	DIM_LUT_V0_INIT(11, 105, GAMMA_2_15, star_a3_s0_rtbl11nit, star_a3_s0_ctbl11nit),
	DIM_LUT_V0_INIT(12, 105, GAMMA_2_15, star_a3_s0_rtbl12nit, star_a3_s0_ctbl12nit),
	DIM_LUT_V0_INIT(13, 105, GAMMA_2_15, star_a3_s0_rtbl13nit, star_a3_s0_ctbl13nit),
	DIM_LUT_V0_INIT(14, 105, GAMMA_2_15, star_a3_s0_rtbl14nit, star_a3_s0_ctbl14nit),
	DIM_LUT_V0_INIT(15, 105, GAMMA_2_15, star_a3_s0_rtbl15nit, star_a3_s0_ctbl15nit),
	DIM_LUT_V0_INIT(16, 105, GAMMA_2_15, star_a3_s0_rtbl16nit, star_a3_s0_ctbl16nit),
	DIM_LUT_V0_INIT(17, 105, GAMMA_2_15, star_a3_s0_rtbl17nit, star_a3_s0_ctbl17nit),
	DIM_LUT_V0_INIT(18, 105, GAMMA_2_15, star_a3_s0_rtbl18nit, star_a3_s0_ctbl18nit),
	DIM_LUT_V0_INIT(19, 105, GAMMA_2_15, star_a3_s0_rtbl19nit, star_a3_s0_ctbl19nit),
	DIM_LUT_V0_INIT(20, 105, GAMMA_2_15, star_a3_s0_rtbl20nit, star_a3_s0_ctbl20nit),
	DIM_LUT_V0_INIT(21, 105, GAMMA_2_15, star_a3_s0_rtbl21nit, star_a3_s0_ctbl21nit),
	DIM_LUT_V0_INIT(23, 105, GAMMA_2_15, star_a3_s0_rtbl23nit, star_a3_s0_ctbl23nit),
	DIM_LUT_V0_INIT(24, 105, GAMMA_2_15, star_a3_s0_rtbl24nit, star_a3_s0_ctbl24nit),
	DIM_LUT_V0_INIT(26, 105, GAMMA_2_15, star_a3_s0_rtbl26nit, star_a3_s0_ctbl26nit),
	DIM_LUT_V0_INIT(27, 105, GAMMA_2_15, star_a3_s0_rtbl27nit, star_a3_s0_ctbl27nit),
	DIM_LUT_V0_INIT(29, 105, GAMMA_2_15, star_a3_s0_rtbl29nit, star_a3_s0_ctbl29nit),
	DIM_LUT_V0_INIT(31, 105, GAMMA_2_15, star_a3_s0_rtbl31nit, star_a3_s0_ctbl31nit),
	DIM_LUT_V0_INIT(33, 105, GAMMA_2_15, star_a3_s0_rtbl33nit, star_a3_s0_ctbl33nit),
	DIM_LUT_V0_INIT(35, 105, GAMMA_2_15, star_a3_s0_rtbl35nit, star_a3_s0_ctbl35nit),
	DIM_LUT_V0_INIT(37, 105, GAMMA_2_15, star_a3_s0_rtbl37nit, star_a3_s0_ctbl37nit),
	DIM_LUT_V0_INIT(39, 105, GAMMA_2_15, star_a3_s0_rtbl39nit, star_a3_s0_ctbl39nit),
	DIM_LUT_V0_INIT(42, 105, GAMMA_2_15, star_a3_s0_rtbl42nit, star_a3_s0_ctbl42nit),
	DIM_LUT_V0_INIT(45, 105, GAMMA_2_15, star_a3_s0_rtbl45nit, star_a3_s0_ctbl45nit),
	DIM_LUT_V0_INIT(48, 105, GAMMA_2_15, star_a3_s0_rtbl48nit, star_a3_s0_ctbl48nit),
	DIM_LUT_V0_INIT(51, 105, GAMMA_2_15, star_a3_s0_rtbl51nit, star_a3_s0_ctbl51nit),
	DIM_LUT_V0_INIT(54, 105, GAMMA_2_15, star_a3_s0_rtbl54nit, star_a3_s0_ctbl54nit),
	DIM_LUT_V0_INIT(57, 105, GAMMA_2_15, star_a3_s0_rtbl57nit, star_a3_s0_ctbl57nit),
	DIM_LUT_V0_INIT(61, 114, GAMMA_2_15, star_a3_s0_rtbl61nit, star_a3_s0_ctbl61nit),
	DIM_LUT_V0_INIT(65, 123, GAMMA_2_15, star_a3_s0_rtbl65nit, star_a3_s0_ctbl65nit),
	DIM_LUT_V0_INIT(69, 126, GAMMA_2_15, star_a3_s0_rtbl69nit, star_a3_s0_ctbl69nit),
	DIM_LUT_V0_INIT(73, 134, GAMMA_2_15, star_a3_s0_rtbl73nit, star_a3_s0_ctbl73nit),
	DIM_LUT_V0_INIT(78, 141, GAMMA_2_15, star_a3_s0_rtbl78nit, star_a3_s0_ctbl78nit),
	DIM_LUT_V0_INIT(83, 151, GAMMA_2_15, star_a3_s0_rtbl83nit, star_a3_s0_ctbl83nit),
	DIM_LUT_V0_INIT(88, 160, GAMMA_2_15, star_a3_s0_rtbl88nit, star_a3_s0_ctbl88nit),
	DIM_LUT_V0_INIT(94, 173, GAMMA_2_15, star_a3_s0_rtbl94nit, star_a3_s0_ctbl94nit),
	DIM_LUT_V0_INIT(100, 184, GAMMA_2_15, star_a3_s0_rtbl100nit, star_a3_s0_ctbl100nit),
	DIM_LUT_V0_INIT(106, 200, GAMMA_2_15, star_a3_s0_rtbl106nit, star_a3_s0_ctbl106nit),
	DIM_LUT_V0_INIT(113, 205, GAMMA_2_15, star_a3_s0_rtbl113nit, star_a3_s0_ctbl113nit),
	DIM_LUT_V0_INIT(120, 219, GAMMA_2_15, star_a3_s0_rtbl120nit, star_a3_s0_ctbl120nit),
	DIM_LUT_V0_INIT(128, 229, GAMMA_2_15, star_a3_s0_rtbl128nit, star_a3_s0_ctbl128nit),
	DIM_LUT_V0_INIT(136, 245, GAMMA_2_15, star_a3_s0_rtbl136nit, star_a3_s0_ctbl136nit),
	DIM_LUT_V0_INIT(145, 253, GAMMA_2_15, star_a3_s0_rtbl145nit, star_a3_s0_ctbl145nit),
	DIM_LUT_V0_INIT(154, 266, GAMMA_2_15, star_a3_s0_rtbl154nit, star_a3_s0_ctbl154nit),
	DIM_LUT_V0_INIT(164, 276, GAMMA_2_15, star_a3_s0_rtbl164nit, star_a3_s0_ctbl164nit),
	DIM_LUT_V0_INIT(174, 295, GAMMA_2_15, star_a3_s0_rtbl174nit, star_a3_s0_ctbl174nit),
	DIM_LUT_V0_INIT(185, 295, GAMMA_2_15, star_a3_s0_rtbl185nit, star_a3_s0_ctbl185nit),
	DIM_LUT_V0_INIT(197, 295, GAMMA_2_15, star_a3_s0_rtbl197nit, star_a3_s0_ctbl197nit),
	DIM_LUT_V0_INIT(210, 295, GAMMA_2_15, star_a3_s0_rtbl210nit, star_a3_s0_ctbl210nit),
	DIM_LUT_V0_INIT(223, 295, GAMMA_2_15, star_a3_s0_rtbl223nit, star_a3_s0_ctbl223nit),
	DIM_LUT_V0_INIT(237, 298, GAMMA_2_15, star_a3_s0_rtbl237nit, star_a3_s0_ctbl237nit),
	DIM_LUT_V0_INIT(253, 307, GAMMA_2_15, star_a3_s0_rtbl253nit, star_a3_s0_ctbl253nit),
	DIM_LUT_V0_INIT(269, 325, GAMMA_2_15, star_a3_s0_rtbl269nit, star_a3_s0_ctbl269nit),
	DIM_LUT_V0_INIT(286, 341, GAMMA_2_15, star_a3_s0_rtbl286nit, star_a3_s0_ctbl286nit),
	DIM_LUT_V0_INIT(301, 347, GAMMA_2_15, star_a3_s0_rtbl301nit, star_a3_s0_ctbl301nit),
	DIM_LUT_V0_INIT(317, 360, GAMMA_2_15, star_a3_s0_rtbl317nit, star_a3_s0_ctbl317nit),
	DIM_LUT_V0_INIT(333, 376, GAMMA_2_15, star_a3_s0_rtbl333nit, star_a3_s0_ctbl333nit),
	DIM_LUT_V0_INIT(340, 390, GAMMA_2_15, star_a3_s0_rtbl340nit, star_a3_s0_ctbl340nit),
	DIM_LUT_V0_INIT(347, 390, GAMMA_2_15, star_a3_s0_rtbl347nit, star_a3_s0_ctbl347nit),
	DIM_LUT_V0_INIT(354, 390, GAMMA_2_15, star_a3_s0_rtbl354nit, star_a3_s0_ctbl354nit),
	DIM_LUT_V0_INIT(362, 390, GAMMA_2_15, star_a3_s0_rtbl362nit, star_a3_s0_ctbl362nit),
	DIM_LUT_V0_INIT(369, 390, GAMMA_2_15, star_a3_s0_rtbl369nit, star_a3_s0_ctbl369nit),
	DIM_LUT_V0_INIT(376, 390, GAMMA_2_15, star_a3_s0_rtbl376nit, star_a3_s0_ctbl376nit),
	DIM_LUT_V0_INIT(384, 390, GAMMA_2_15, star_a3_s0_rtbl384nit, star_a3_s0_ctbl384nit),
	DIM_LUT_V0_INIT(392, 393, GAMMA_2_15, star_a3_s0_rtbl392nit, star_a3_s0_ctbl392nit),
	DIM_LUT_V0_INIT(400, 400, GAMMA_2_20, star_a3_s0_rtbl400nit, star_a3_s0_ctbl400nit),
};


#endif /* __EXYNOS9810_STAR_PANEL_DIMMING_H__ */
