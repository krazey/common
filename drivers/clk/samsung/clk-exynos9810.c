// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Mathias Gluszczynski <admin@krazey.de>
 *
 * Common Clock Framework support for the Samsung Exynos9810 SoC.
 */

#include <linux/clk-provider.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/samsung,exynos9810.h>

#include "clk.h"
#include "clk-exynos-arm64.h"

#define CLKS_NR_TOP		(CLK_DOUT_TOP_FSYS1_PCIE + 1)
#define CLKS_NR_FSYS0		(CLK_GOUT_FSYS0_USB30DRD_CTRL + 1)
#define CLKS_NR_FSYS1		(CLK_GOUT_FSYS1_PCIE_SLV + 1)
#define CLKS_NR_PERIC0		(CLK_GOUT_PERIC0_USI1_PCLK + 1)
#define CLKS_NR_CMGP		(CLK_GOUT_CMGP_USI3_PCLK + 1)
#define CLKS_NR_DPU		(CLK_GOUT_DPU_SYSMMU_DPUD1_QCH + 1)
#define CLKS_NR_AUD		(CLK_GOUT_AUD_CPU + 1)

/* ---- CMU_TOP ---------------------------------------------------------- */

#define CLK_CON_MUX_MUX_CLKCMU_FSYS1_PCIE	0x1070
#define CLK_CON_GAT_GATE_CLKCMU_CMGP_BUS		0x2028
#define CLK_CON_GAT_GATE_CLKCMU_DPU_BUS		0x204c
#define CLK_CON_GAT_GATE_CLKCMU_FSYS1_PCIE	0x2074

static const unsigned long top_clk_regs[] __initconst = {
	CLK_CON_MUX_MUX_CLKCMU_FSYS1_PCIE,
	CLK_CON_GAT_GATE_CLKCMU_CMGP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DPU_BUS,
	CLK_CON_GAT_GATE_CLKCMU_FSYS1_PCIE,
};

static const struct samsung_fixed_rate_clock top_fixed_clks[] __initconst = {
	FRATE(0, "fout_shared2_pll_boot", NULL, 0, 800 * MHZ),
};

PNAME(mout_top_fsys1_pcie_p) = {
	"oscclk", "fout_shared2_pll_boot"
};

static const struct samsung_mux_clock top_mux_clks[] __initconst = {
	MUX(CLK_MOUT_TOP_FSYS1_PCIE, "mout_clkcmu_fsys1_pcie",
	    mout_top_fsys1_pcie_p, CLK_CON_MUX_MUX_CLKCMU_FSYS1_PCIE,
	    0, 1),
};

static const struct samsung_fixed_factor_clock top_fixed_factor_clks[] __initconst = {
	FFACTOR(CLK_DOUT_TOP_FSYS1_PCIE, "dout_clkcmu_fsys1_pcie",
		"gout_clkcmu_fsys1_pcie", 1, 8, 0),
};

static const struct samsung_gate_clock top_gate_clks[] __initconst = {
	GATE(CLK_GOUT_TOP_FSYS1_PCIE, "gout_clkcmu_fsys1_pcie",
	     "mout_clkcmu_fsys1_pcie",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS1_PCIE,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_TOP_CMGP_BUS, "dout_clkcmu_cmgp_bus",
	     "cmgp_bus_bootclk", CLK_CON_GAT_GATE_CLKCMU_CMGP_BUS,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_TOP_DPU_BUS, "dout_clkcmu_dpu_bus",
	     "dpu_bus_bootclk", CLK_CON_GAT_GATE_CLKCMU_DPU_BUS,
	     21, CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info top_cmu_info __initconst = {
	.mux_clks		= top_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(top_mux_clks),
	.fixed_clks		= top_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(top_fixed_clks),
	.fixed_factor_clks	= top_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(top_fixed_factor_clks),
	.gate_clks		= top_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(top_gate_clks),
	.nr_clk_ids		= CLKS_NR_TOP,
	.clk_regs		= top_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(top_clk_regs),
	.clk_name		= "cmgp_bus_bootclk",
};

/* ---- CMU_FSYS0 --------------------------------------------------------- */

#define PLL_CON0_MUX_CLKCMU_FSYS0_BUS_USER		0x0100
#define PLL_CON0_MUX_CLKCMU_FSYS0_UFS_EMBD_USER		0x0180
#define PLL_CON0_MUX_CLKCMU_FSYS0_USB30DRD_USER		0x01e0
#define CLK_CON_GAT_GOUT_FSYS0_UFS_EMBD_UNIPRO		0x205c
#define CLK_CON_GAT_GOUT_FSYS0_USB30DRD_REF		0x206c
#define QCH_CON_UFS_EMBD					0x3044
#define QCH_CON_USB30DRD_CTRL				0x304c
#define QCH_CON_USB30DRD_LINK				0x3050

static const unsigned long fsys0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_FSYS0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_FSYS0_UFS_EMBD_USER,
	PLL_CON0_MUX_CLKCMU_FSYS0_USB30DRD_USER,
	CLK_CON_GAT_GOUT_FSYS0_UFS_EMBD_UNIPRO,
	CLK_CON_GAT_GOUT_FSYS0_USB30DRD_REF,
	QCH_CON_UFS_EMBD,
	QCH_CON_USB30DRD_CTRL,
	QCH_CON_USB30DRD_LINK,
};

PNAME(mout_fsys0_bus_user_p) = {
	"oscclk", "dout_clkcmu_fsys0_bus"
};

PNAME(mout_fsys0_ufs_embd_user_p) = {
	"oscclk", "dout_clkcmu_fsys0_ufs_embd"
};

PNAME(mout_fsys0_usb30drd_user_p) = {
	"oscclk", "dout_clkcmu_fsys0_usb30drd"
};

static const struct samsung_mux_clock fsys0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_FSYS0_BUS_USER, "mout_fsys0_bus_user",
	    mout_fsys0_bus_user_p, PLL_CON0_MUX_CLKCMU_FSYS0_BUS_USER,
	    4, 1),
	MUX(CLK_MOUT_FSYS0_UFS_EMBD_USER, "mout_fsys0_ufs_embd_user",
	    mout_fsys0_ufs_embd_user_p,
	    PLL_CON0_MUX_CLKCMU_FSYS0_UFS_EMBD_USER, 4, 1),
	MUX(CLK_MOUT_FSYS0_USB30DRD_USER, "mout_fsys0_usb30drd_user",
	    mout_fsys0_usb30drd_user_p,
	    PLL_CON0_MUX_CLKCMU_FSYS0_USB30DRD_USER, 4, 1),
};

static const struct samsung_gate_clock fsys0_gate_clks[] __initconst = {
	/* QCH request controls the UFS bus clock when HWACG is disabled. */
	GATE(CLK_GOUT_FSYS0_UFS_EMBD_ACLK,
	     "gout_fsys0_ufs_embd_aclk", "mout_fsys0_bus_user",
	     QCH_CON_UFS_EMBD, 1, 0, 0),
	GATE(CLK_GOUT_FSYS0_UFS_EMBD_UNIPRO,
	     "gout_fsys0_ufs_embd_unipro", "mout_fsys0_ufs_embd_user",
	     CLK_CON_GAT_GOUT_FSYS0_UFS_EMBD_UNIPRO, 21, 0, 0),
	/* Keep the USB Q-channel requests asserted while DWC3 is active. */
	GATE(CLK_GOUT_FSYS0_USB30DRD_LINK,
	     "gout_fsys0_usb30drd_link", "mout_fsys0_usb30drd_user",
	     QCH_CON_USB30DRD_LINK, 1, 0, 0),
	GATE(CLK_GOUT_FSYS0_USB30DRD_CTRL,
	     "gout_fsys0_usb30drd_ctrl", "mout_fsys0_usb30drd_user",
	     QCH_CON_USB30DRD_CTRL, 1, 0, 0),
	GATE(CLK_GOUT_FSYS0_USB30DRD_REF,
	     "gout_fsys0_usb30drd_ref", "mout_fsys0_usb30drd_user",
	     CLK_CON_GAT_GOUT_FSYS0_USB30DRD_REF, 21, 0, 0),
};

static const struct samsung_cmu_info fsys0_cmu_info __initconst = {
	.mux_clks		= fsys0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys0_mux_clks),
	.gate_clks		= fsys0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys0_gate_clks),
	.nr_clk_ids		= CLKS_NR_FSYS0,
	.clk_regs		= fsys0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys0_clk_regs),
	.clk_name		= "dout_clkcmu_fsys0_bus",
};

/* ---- CMU_FSYS1 -------------------------------------------------------- */

#define PLL_CON0_MUX_CLKCMU_FSYS1_BUS_USER		0x0100
#define PLL_CON0_MUX_CLKCMU_FSYS1_PCIE_USER		0x0180
#define CLK_CON_GAT_FSYS1_PCIE_PHY_REF			0x2000
#define CLK_CON_GAT_FSYS1_PCIE_DBI			0x2038
#define CLK_CON_GAT_FSYS1_PCIE_PHY_APB			0x203c
#define CLK_CON_GAT_FSYS1_PCIE_MSTR			0x2040
#define CLK_CON_GAT_FSYS1_PCIE_SUBCTRL			0x2044
#define CLK_CON_GAT_FSYS1_PCIE_PCS			0x2048
#define CLK_CON_GAT_FSYS1_PCIE_SLV			0x204c

static const unsigned long fsys1_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_FSYS1_BUS_USER,
	PLL_CON0_MUX_CLKCMU_FSYS1_PCIE_USER,
	CLK_CON_GAT_FSYS1_PCIE_PHY_REF,
	CLK_CON_GAT_FSYS1_PCIE_DBI,
	CLK_CON_GAT_FSYS1_PCIE_PHY_APB,
	CLK_CON_GAT_FSYS1_PCIE_MSTR,
	CLK_CON_GAT_FSYS1_PCIE_SUBCTRL,
	CLK_CON_GAT_FSYS1_PCIE_PCS,
	CLK_CON_GAT_FSYS1_PCIE_SLV,
};

PNAME(mout_fsys1_bus_user_p) = {
	"oscclk", "dout_clkcmu_fsys1_bus"
};

PNAME(mout_fsys1_pcie_user_p) = {
	"oscclk", "dout_clkcmu_fsys1_pcie"
};

static const struct samsung_mux_clock fsys1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_FSYS1_BUS_USER, "mout_fsys1_bus_user",
	    mout_fsys1_bus_user_p, PLL_CON0_MUX_CLKCMU_FSYS1_BUS_USER,
	    4, 1),
	MUX(CLK_MOUT_FSYS1_PCIE_USER, "mout_fsys1_pcie_user",
	    mout_fsys1_pcie_user_p, PLL_CON0_MUX_CLKCMU_FSYS1_PCIE_USER,
	    4, 1),
};

static const struct samsung_gate_clock fsys1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_FSYS1_PCIE_PHY_REF,
	     "gout_fsys1_pcie_phy_ref", "mout_fsys1_pcie_user",
	     CLK_CON_GAT_FSYS1_PCIE_PHY_REF, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_DBI,
	     "gout_fsys1_pcie_dbi", "mout_fsys1_bus_user",
	     CLK_CON_GAT_FSYS1_PCIE_DBI, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_PHY_APB,
	     "gout_fsys1_pcie_phy_apb", "mout_fsys1_bus_user",
	     CLK_CON_GAT_FSYS1_PCIE_PHY_APB, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_MSTR,
	     "gout_fsys1_pcie_mstr", "mout_fsys1_bus_user",
	     CLK_CON_GAT_FSYS1_PCIE_MSTR, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_SUBCTRL,
	     "gout_fsys1_pcie_subctrl", "mout_fsys1_bus_user",
	     CLK_CON_GAT_FSYS1_PCIE_SUBCTRL, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_PCS,
	     "gout_fsys1_pcie_pcs", "mout_fsys1_bus_user",
	     CLK_CON_GAT_FSYS1_PCIE_PCS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_SLV,
	     "gout_fsys1_pcie_slv", "mout_fsys1_bus_user",
	     CLK_CON_GAT_FSYS1_PCIE_SLV, 21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info fsys1_cmu_info __initconst = {
	.mux_clks		= fsys1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys1_mux_clks),
	.gate_clks		= fsys1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys1_gate_clks),
	.nr_clk_ids		= CLKS_NR_FSYS1,
	.clk_regs		= fsys1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys1_clk_regs),
	.clk_name		= "dout_clkcmu_fsys1_bus",
};

/* ---- CMU_DPU ---------------------------------------------------------- */

/*
 * The Exynos9810 clock data places the DPU bus user mux at 0x0100 and the
 * non-secure DPUD1 APB adapter clock gate at 0x205c.
 *
 * Keep the clocks critical while the driver hands the display state left by
 * the bootloader to the kernel.
 */
#define PLL_CON0_MUX_CLKCMU_DPU_BUS_USER		0x0100
#define CLK_CON_GAT_GOUT_DPU_SYSMMU_DPUD1_PCLK		0x205c
#define QCH_CON_SYSMMU_DPUD1			0x3050

static const unsigned long dpu_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_DPU_BUS_USER,
	CLK_CON_GAT_GOUT_DPU_SYSMMU_DPUD1_PCLK,
	QCH_CON_SYSMMU_DPUD1,
};

PNAME(mout_dpu_bus_user_p) = {
	"oscclk", "dout_clkcmu_dpu_bus"
};

static const struct samsung_mux_clock dpu_mux_clks[] __initconst = {
	MUX(CLK_MOUT_DPU_BUS_USER, "mout_dpu_bus_user",
	    mout_dpu_bus_user_p, PLL_CON0_MUX_CLKCMU_DPU_BUS_USER,
	    4, 1),
};

static const struct samsung_gate_clock dpu_gate_clks[] __initconst = {
	GATE(CLK_GOUT_DPU_SYSMMU_DPUD1_PCLK,
	     "gout_dpu_sysmmu_dpud1_pclk", "mout_dpu_bus_user",
	     CLK_CON_GAT_GOUT_DPU_SYSMMU_DPUD1_PCLK,
	     21, CLK_IS_CRITICAL, 0),
	/*
	 * Samsung CAL exposes GATE_SYSMMU_DPUD1 through SYSMMU_DPUD1_QCH.
	 * Force the Q-channel into software-request mode before asserting the
	 * request, matching the established Exynos9810 CCF Q-channel model.
	 */
	GATE(0, "gout_dpu_sysmmu_dpud1_qch_ignore",
	     "mout_dpu_bus_user", QCH_CON_SYSMMU_DPUD1,
	     2, CLK_IS_CRITICAL, 0),
	GATE(0, "gout_dpu_sysmmu_dpud1_qch_mode",
	     "gout_dpu_sysmmu_dpud1_qch_ignore", QCH_CON_SYSMMU_DPUD1,
	     0, CLK_IS_CRITICAL, CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GOUT_DPU_SYSMMU_DPUD1_QCH,
	     "gout_dpu_sysmmu_dpud1_qch",
	     "gout_dpu_sysmmu_dpud1_qch_mode", QCH_CON_SYSMMU_DPUD1,
	     1, CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info dpu_cmu_info __initconst = {
	.mux_clks		= dpu_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dpu_mux_clks),
	.gate_clks		= dpu_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(dpu_gate_clks),
	.nr_clk_ids		= CLKS_NR_DPU,
	.clk_regs		= dpu_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dpu_clk_regs),
	.clk_name		= "dout_clkcmu_dpu_bus",
};

/* ---- CMU_AUD --------------------------------------------------------- */

#define PLL_LOCKTIME_PLL_AUD				0x0000
#define PLL_CON0_PLL_AUD				0x0120
#define PLL_CON3_PLL_AUD				0x018c
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF0			0x1004
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF1			0x1008
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF2			0x100c
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF3			0x1010
#define CLK_CON_DIV_DIV_CLK_AUD_AUDIF			0x1800
#define CLK_CON_DIV_DIV_CLK_AUD_BUS			0x1804
#define CLK_CON_DIV_DIV_CLK_AUD_BUSP			0x1808
#define CLK_CON_DIV_DIV_CLK_AUD_DMIC			0x1818
#define CLK_CON_DIV_DIV_CLK_AUD_DSIF			0x181c
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF0			0x1824
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF1			0x1828
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF2			0x182c
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF3			0x1830
#define CLK_CON_GAT_AUD_CODEC_MCLK			0x2004
#define CLK_CON_GAT_AUD_DMIC				0x2008
#define CLK_CON_GAT_AUD_DSIF				0x2018
#define CLK_CON_GAT_AUD_UAIF0				0x201c
#define CLK_CON_GAT_AUD_UAIF1				0x2020
#define CLK_CON_GAT_AUD_UAIF2				0x2024
#define CLK_CON_GAT_AUD_UAIF3				0x2028
#define CLK_CON_GAT_AUD_SYSMMU_PCLK			0x203c
#define DMYQCH_CON_ABOX_CPU				0x3000
#define DMYQCH_CON_DMIC					0x3008
#define QCH_CON_ABOX_ACLK				0x3024
#define QCH_CON_ABOX_BCLK0				0x3028
#define QCH_CON_ABOX_BCLK1				0x302c
#define QCH_CON_ABOX_BCLK2				0x3030
#define QCH_CON_ABOX_BCLK3				0x3034
#define QCH_CON_ABOX_BCLK_DSIF				0x3038
#define QCH_CON_SYSMMU_AUD				0x3060

static const unsigned long aud_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_AUD,
	PLL_CON0_PLL_AUD,
	PLL_CON3_PLL_AUD,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF0,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF1,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF2,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF3,
	CLK_CON_DIV_DIV_CLK_AUD_AUDIF,
	CLK_CON_DIV_DIV_CLK_AUD_BUS,
	CLK_CON_DIV_DIV_CLK_AUD_BUSP,
	CLK_CON_DIV_DIV_CLK_AUD_DMIC,
	CLK_CON_DIV_DIV_CLK_AUD_DSIF,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF0,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF1,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF2,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF3,
	CLK_CON_GAT_AUD_CODEC_MCLK,
	CLK_CON_GAT_AUD_DMIC,
	CLK_CON_GAT_AUD_DSIF,
	CLK_CON_GAT_AUD_UAIF0,
	CLK_CON_GAT_AUD_UAIF1,
	CLK_CON_GAT_AUD_UAIF2,
	CLK_CON_GAT_AUD_UAIF3,
	CLK_CON_GAT_AUD_SYSMMU_PCLK,
	DMYQCH_CON_ABOX_CPU,
	DMYQCH_CON_DMIC,
	QCH_CON_ABOX_ACLK,
	QCH_CON_ABOX_BCLK0,
	QCH_CON_ABOX_BCLK1,
	QCH_CON_ABOX_BCLK2,
	QCH_CON_ABOX_BCLK3,
	QCH_CON_ABOX_BCLK_DSIF,
	QCH_CON_SYSMMU_AUD,
};

/* Rates are the exact values requested by the stock ABOX driver. */
static const struct samsung_pll_rate_table aud_pll_rates[] __initconst = {
	{
		.rate = 1179648040U,
		.mdiv = 45,
		.pdiv = 1,
		.sdiv = 0,
		.kdiv = 24319,
	}, {
		.rate = 1083801600U,
		.mdiv = 42,
		.pdiv = 1,
		.sdiv = 0,
		.kdiv = 0xaf47,
	}, {
	},
};

static const struct samsung_pll_clock aud_pll_clks[] __initconst = {
	PLL_KDIV(pll_1031x, CLK_FOUT_AUD_PLL, "fout_aud_pll", "oscclk",
		  PLL_LOCKTIME_PLL_AUD, PLL_CON0_PLL_AUD, 0x6c, 29,
		  aud_pll_rates),
};

static const struct samsung_fixed_rate_clock aud_fixed_clks[] __initconst = {
	FRATE(0, "ioclk_aud_uaif0", NULL, 0, 10 * MHZ),
	FRATE(0, "ioclk_aud_uaif1", NULL, 0, 10 * MHZ),
	FRATE(0, "ioclk_aud_uaif2", NULL, 0, 10 * MHZ),
	FRATE(0, "ioclk_aud_uaif3", NULL, 0, 100 * MHZ),
};

PNAME(mout_aud_uaif0_p) = {
	"dout_aud_uaif0", "ioclk_aud_uaif0"
};

PNAME(mout_aud_uaif1_p) = {
	"dout_aud_uaif1", "ioclk_aud_uaif1"
};

PNAME(mout_aud_uaif2_p) = {
	"dout_aud_uaif2", "ioclk_aud_uaif2"
};

PNAME(mout_aud_uaif3_p) = {
	"dout_aud_uaif3", "ioclk_aud_uaif3"
};

static const struct samsung_mux_clock aud_mux_clks[] __initconst = {
	MUX(CLK_MOUT_AUD_UAIF0, "mout_aud_uaif0", mout_aud_uaif0_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF0, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF1, "mout_aud_uaif1", mout_aud_uaif1_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF1, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF2, "mout_aud_uaif2", mout_aud_uaif2_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF2, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF3, "mout_aud_uaif3", mout_aud_uaif3_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF3, 0, 1),
};

static const struct samsung_div_clock aud_div_clks[] __initconst = {
	DIV(CLK_DOUT_AUD_AUDIF, "dout_aud_audif", "fout_aud_pll",
	    CLK_CON_DIV_DIV_CLK_AUD_AUDIF, 0, 9),
	DIV(CLK_DOUT_AUD_BUS, "dout_aud_bus", "fout_aud_pll",
	    CLK_CON_DIV_DIV_CLK_AUD_BUS, 0, 3),
	DIV(CLK_DOUT_AUD_BUSP, "dout_aud_busp", "dout_aud_bus",
	    CLK_CON_DIV_DIV_CLK_AUD_BUSP, 0, 2),
	DIV(CLK_DOUT_AUD_DMIC, "dout_aud_dmic", "dout_aud_dsif",
	    CLK_CON_DIV_DIV_CLK_AUD_DMIC, 0, 2),
	DIV(CLK_DOUT_AUD_DSIF, "dout_aud_dsif", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_DSIF, 0, 5),
	DIV(CLK_DOUT_AUD_UAIF0, "dout_aud_uaif0", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF0, 0, 9),
	DIV(CLK_DOUT_AUD_UAIF1, "dout_aud_uaif1", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF1, 0, 9),
	DIV(CLK_DOUT_AUD_UAIF2, "dout_aud_uaif2", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF2, 0, 9),
	DIV(CLK_DOUT_AUD_UAIF3, "dout_aud_uaif3", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF3, 0, 9),
};

static const struct samsung_gate_clock aud_gate_clks[] __initconst = {
	GATE(CLK_GOUT_AUD_UAIF0, "gout_aud_uaif0", "mout_aud_uaif0",
	     QCH_CON_ABOX_BCLK0, 1, 0, 0),
	GATE(CLK_GOUT_AUD_UAIF1, "gout_aud_uaif1", "mout_aud_uaif1",
	     QCH_CON_ABOX_BCLK1, 1, 0, 0),
	GATE(CLK_GOUT_AUD_UAIF2, "gout_aud_uaif2", "mout_aud_uaif2",
	     QCH_CON_ABOX_BCLK2, 1, 0, 0),
	GATE(CLK_GOUT_AUD_UAIF3, "gout_aud_uaif3", "mout_aud_uaif3",
	     QCH_CON_ABOX_BCLK3, 1, 0, 0),
	GATE(CLK_GOUT_AUD_DSIF, "gout_aud_dsif", "dout_aud_dsif",
	     QCH_CON_ABOX_BCLK_DSIF, 1, 0, 0),
	GATE(CLK_GOUT_AUD_ABOX_ACLK, "gout_aud_abox_aclk",
	     "dout_aud_bus", QCH_CON_ABOX_ACLK, 1,
	     CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_AUD_SYSMMU_ACLK, "gout_aud_sysmmu_aclk",
	     "dout_aud_bus", QCH_CON_SYSMMU_AUD, 1,
	     CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_AUD_SYSMMU_PCLK, "gout_aud_sysmmu_pclk",
	     "dout_aud_busp", CLK_CON_GAT_AUD_SYSMMU_PCLK, 21,
	     CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_AUD_CODEC_MCLK, "gout_aud_codec_mclk",
	     "dout_aud_audif", CLK_CON_GAT_AUD_CODEC_MCLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_DMIC, "gout_aud_dmic", "dout_aud_dmic",
	     DMYQCH_CON_DMIC, 1, 0, 0),
	GATE(CLK_GOUT_AUD_CPU, "gout_aud_cpu", "dout_aud_bus",
	     DMYQCH_CON_ABOX_CPU, 1, CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info aud_cmu_info __initconst = {
	.pll_clks		= aud_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(aud_pll_clks),
	.fixed_clks		= aud_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(aud_fixed_clks),
	.mux_clks		= aud_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(aud_mux_clks),
	.div_clks		= aud_div_clks,
	.nr_div_clks		= ARRAY_SIZE(aud_div_clks),
	.gate_clks		= aud_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(aud_gate_clks),
	.nr_clk_ids		= CLKS_NR_AUD,
	.clk_regs		= aud_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(aud_clk_regs),
	.clk_name		= "oscclk",
};

/* ---- CMU_PERIC0 -------------------------------------------------------- */

#define PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER		0x0100
#define PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER		0x0120
#define CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG		0x1800
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI01			0x1808
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI03			0x1810
#define CLK_CON_GAT_GATE_PERIC0_UART_DBG			0x2008
#define CLK_CON_GAT_GATE_CLK_PERIC0_USI01		0x2010
#define CLK_CON_GAT_GATE_CLK_PERIC0_USI03		0x2018
#define CLK_CON_GAT_GOUT_PERIC0_UART_DBG_RST		0x204c
#define CLK_CON_GAT_GOUT_PERIC0_USI01_RST		0x205c
#define CLK_CON_GAT_GOUT_PERIC0_USI03_RST		0x206c
#define CLK_CON_GAT_GOUT_PERIC0_SYSREG_PCLK		0x2098
#define CLK_CON_GAT_GOUT_PERIC0_UART_DBG_IPCLK		0x209c
#define CLK_CON_GAT_GOUT_PERIC0_UART_DBG_PCLK		0x20a0
#define CLK_CON_GAT_GOUT_PERIC0_USI01_IPCLK		0x20bc
#define CLK_CON_GAT_GOUT_PERIC0_USI01_PCLK		0x20c0
#define CLK_CON_GAT_GOUT_PERIC0_USI03_IPCLK		0x20dc
#define CLK_CON_GAT_GOUT_PERIC0_USI03_PCLK		0x20e0
#define QCH_CON_SYSREG_PERIC0				0x3014
#define QCH_CON_UART_DBG					0x3018
#define QCH_CON_USI01					0x3028
#define QCH_CON_USI03					0x3038

static const unsigned long peric0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER,
	CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI01,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI03,
	CLK_CON_GAT_GATE_PERIC0_UART_DBG,
	CLK_CON_GAT_GATE_CLK_PERIC0_USI01,
	CLK_CON_GAT_GATE_CLK_PERIC0_USI03,
	CLK_CON_GAT_GOUT_PERIC0_UART_DBG_RST,
	CLK_CON_GAT_GOUT_PERIC0_USI01_RST,
	CLK_CON_GAT_GOUT_PERIC0_USI03_RST,
	CLK_CON_GAT_GOUT_PERIC0_SYSREG_PCLK,
	CLK_CON_GAT_GOUT_PERIC0_UART_DBG_IPCLK,
	CLK_CON_GAT_GOUT_PERIC0_UART_DBG_PCLK,
	CLK_CON_GAT_GOUT_PERIC0_USI01_IPCLK,
	CLK_CON_GAT_GOUT_PERIC0_USI01_PCLK,
	CLK_CON_GAT_GOUT_PERIC0_USI03_IPCLK,
	CLK_CON_GAT_GOUT_PERIC0_USI03_PCLK,
	QCH_CON_SYSREG_PERIC0,
	QCH_CON_UART_DBG,
	QCH_CON_USI01,
	QCH_CON_USI03,
};

PNAME(mout_peric0_bus_user_p) = {
	"oscclk", "dout_clkcmu_peric0_bus"
};

PNAME(mout_peric0_ip_user_p) = {
	"oscclk", "dout_clkcmu_peric0_ip"
};

static const struct samsung_mux_clock peric0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIC0_BUS_USER, "mout_peric0_bus_user",
	    mout_peric0_bus_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_IP_USER, "mout_peric0_ip_user",
	    mout_peric0_ip_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER,
	    4, 1),
};

/*
 * S-Boot configures the peripheral dividers before entering Linux. Register
 * them without forcing new rates so the early console and USI retain their
 * working clocks.
 */
static const struct samsung_div_clock peric0_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERIC0_UART_DBG, "dout_peric0_uart_dbg",
	    "gout_peric0_uart_dbg", CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI1, "dout_peric0_usi1",
	    "gout_peric0_usi1", CLK_CON_DIV_DIV_CLK_PERIC0_USI01,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI3, "dout_peric0_usi3",
	    "gout_peric0_usi3", CLK_CON_DIV_DIV_CLK_PERIC0_USI03,
	    0, 4),
};

static const struct samsung_gate_clock peric0_gate_clks[] __initconst = {
	/* Keep SYSREG accessible while USI protocol selection is active. */
	GATE(0, "gout_peric0_sysreg_qch_ignore", "mout_peric0_bus_user",
	     QCH_CON_SYSREG_PERIC0, 2, CLK_IS_CRITICAL, 0),
	GATE(0, "gout_peric0_sysreg_qch_mode",
	     "gout_peric0_sysreg_qch_ignore", QCH_CON_SYSREG_PERIC0,
	     0, CLK_IS_CRITICAL, CLK_GATE_SET_TO_DISABLE),
	GATE(0, "gout_peric0_sysreg_qch", "gout_peric0_sysreg_qch_mode",
	     QCH_CON_SYSREG_PERIC0, 1, CLK_IS_CRITICAL, 0),
	GATE(0, "gout_peric0_sysreg_pclk", "gout_peric0_sysreg_qch",
	     CLK_CON_GAT_GOUT_PERIC0_SYSREG_PCLK, 21,
	     CLK_IS_CRITICAL, 0),
	/* Hold the UART clock request when Q-channel HWACG is disabled. */
	GATE(0, "gout_peric0_uart_dbg_qch", "mout_peric0_bus_user",
	     QCH_CON_UART_DBG, 1, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC0_UART_DBG, "gout_peric0_uart_dbg",
	     "mout_peric0_ip_user", CLK_CON_GAT_GATE_PERIC0_UART_DBG,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC0_UART_DBG_RST, "gout_peric0_uart_dbg_rst",
	     "dout_peric0_uart_dbg", CLK_CON_GAT_GOUT_PERIC0_UART_DBG_RST,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC0_UART_DBG_PCLK, "gout_peric0_uart_dbg_pclk",
	     "mout_peric0_bus_user", CLK_CON_GAT_GOUT_PERIC0_UART_DBG_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_UART_DBG_IPCLK, "gout_peric0_uart_dbg_ipclk",
	     "dout_peric0_uart_dbg", CLK_CON_GAT_GOUT_PERIC0_UART_DBG_IPCLK,
	     21, 0, 0),
	/* USI1 provides the SPI link used by the CS47L93 codec. */
	GATE(0, "gout_peric0_usi1_qch_ignore", "mout_peric0_bus_user",
	     QCH_CON_USI01, 2, CLK_IS_CRITICAL, 0),
	GATE(0, "gout_peric0_usi1_qch_mode",
	     "gout_peric0_usi1_qch_ignore", QCH_CON_USI01,
	     0, CLK_IS_CRITICAL, CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GOUT_PERIC0_USI1_QCH, "gout_peric0_usi1_qch",
	     "gout_peric0_usi1_qch_mode", QCH_CON_USI01, 1, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI1, "gout_peric0_usi1",
	     "mout_peric0_ip_user", CLK_CON_GAT_GATE_CLK_PERIC0_USI01,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI1_RST, "gout_peric0_usi1_rst",
	     "dout_peric0_usi1", CLK_CON_GAT_GOUT_PERIC0_USI01_RST,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI1_IPCLK, "gout_peric0_usi1_ipclk",
	     "gout_peric0_usi1_rst", CLK_CON_GAT_GOUT_PERIC0_USI01_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI1_PCLK, "gout_peric0_usi1_pclk",
	     "gout_peric0_usi1_qch", CLK_CON_GAT_GOUT_PERIC0_USI01_PCLK,
	     21, 0, 0),
	/* USI3 provides the HSI2C10 link used by the touchscreen. */
	GATE(0, "gout_peric0_usi3_qch_ignore", "mout_peric0_bus_user",
	     QCH_CON_USI03, 2, CLK_IS_CRITICAL, 0),
	GATE(0, "gout_peric0_usi3_qch_mode",
	     "gout_peric0_usi3_qch_ignore", QCH_CON_USI03,
	     0, CLK_IS_CRITICAL, CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GOUT_PERIC0_USI3_QCH, "gout_peric0_usi3_qch",
	     "gout_peric0_usi3_qch_mode", QCH_CON_USI03, 1, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI3, "gout_peric0_usi3",
	     "mout_peric0_ip_user", CLK_CON_GAT_GATE_CLK_PERIC0_USI03,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI3_RST, "gout_peric0_usi3_rst",
	     "dout_peric0_usi3", CLK_CON_GAT_GOUT_PERIC0_USI03_RST,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI3_IPCLK, "gout_peric0_usi3_ipclk",
	     "gout_peric0_usi3_rst", CLK_CON_GAT_GOUT_PERIC0_USI03_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI3_PCLK, "gout_peric0_usi3_pclk",
	     "gout_peric0_usi3_qch", CLK_CON_GAT_GOUT_PERIC0_USI03_PCLK,
	     21, 0, 0),
};

static const struct samsung_cmu_info peric0_cmu_info __initconst = {
	.mux_clks		= peric0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peric0_mux_clks),
	.div_clks		= peric0_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peric0_div_clks),
	.gate_clks		= peric0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peric0_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERIC0,
	.clk_regs		= peric0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric0_clk_regs),
	.clk_name		= "dout_clkcmu_peric0_bus",
};

/* ---- CMU_CMGP --------------------------------------------------------- */

#define PLL_CON0_MUX_CLKCMU_CMGP_BUS_USER		0x0100
#define CLK_CON_MUX_MUX_CLK_USI_CMGP03			0x1018
#define CLK_CON_DIV_DIV_CLK_USI_CMGP03			0x1814
#define CLK_CON_GAT_GATE_CLK_USI_CMGP03			0x2014
#define CLK_CON_GAT_GOUT_USI_CMGP03_RST			0x2074
#define CLK_CON_GAT_GOUT_USI_CMGP03_IPCLK		0x20a8
#define CLK_CON_GAT_GOUT_USI_CMGP03_PCLK			0x20ac
#define QCH_CON_USI_CMGP03				0x3050

static const unsigned long cmgp_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_CMGP_BUS_USER,
	CLK_CON_MUX_MUX_CLK_USI_CMGP03,
	CLK_CON_DIV_DIV_CLK_USI_CMGP03,
	CLK_CON_GAT_GATE_CLK_USI_CMGP03,
	CLK_CON_GAT_GOUT_USI_CMGP03_RST,
	CLK_CON_GAT_GOUT_USI_CMGP03_IPCLK,
	CLK_CON_GAT_GOUT_USI_CMGP03_PCLK,
	QCH_CON_USI_CMGP03,
};

PNAME(mout_cmgp_bus_user_p) = {
	"oscclk", "dout_clkcmu_cmgp_bus"
};

PNAME(mout_cmgp_usi3_p) = {
	"oscclk", "gout_cmgp_usi3_qch"
};

static const struct samsung_mux_clock cmgp_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMGP_BUS_USER, "mout_cmgp_bus_user",
	    mout_cmgp_bus_user_p, PLL_CON0_MUX_CLKCMU_CMGP_BUS_USER,
	    4, 1),
	MUX(CLK_MOUT_CMGP_USI3, "mout_cmgp_usi3", mout_cmgp_usi3_p,
	    CLK_CON_MUX_MUX_CLK_USI_CMGP03, 0, 1),
};

static const struct samsung_div_clock cmgp_div_clks[] __initconst = {
	DIV(CLK_DOUT_CMGP_USI3, "dout_cmgp_usi3", "gout_cmgp_usi3",
	    CLK_CON_DIV_DIV_CLK_USI_CMGP03, 0, 4),
};

static const struct samsung_gate_clock cmgp_gate_clks[] __initconst = {
	/*
	 * Disable Q-channel HWACG so CCF can hold the software clock request
	 * while either USI clock is used. Ignore force-PM gating in this mode,
	 * matching the state programmed by Samsung's CAL implementation.
	 */
	GATE(0, "gout_cmgp_usi3_qch_ignore", "mout_cmgp_bus_user",
	     QCH_CON_USI_CMGP03, 2, CLK_IS_CRITICAL, 0),
	GATE(0, "gout_cmgp_usi3_qch_mode", "gout_cmgp_usi3_qch_ignore",
	     QCH_CON_USI_CMGP03, 0, CLK_IS_CRITICAL,
	     CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GOUT_CMGP_USI3_QCH, "gout_cmgp_usi3_qch",
	     "gout_cmgp_usi3_qch_mode", QCH_CON_USI_CMGP03, 1, 0, 0),
	GATE(CLK_GOUT_CMGP_USI3, "gout_cmgp_usi3", "mout_cmgp_usi3",
	     CLK_CON_GAT_GATE_CLK_USI_CMGP03, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI3_RST, "gout_cmgp_usi3_rst",
	     "dout_cmgp_usi3", CLK_CON_GAT_GOUT_USI_CMGP03_RST,
	     21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI3_IPCLK, "gout_cmgp_usi3_ipclk",
	     "gout_cmgp_usi3_rst", CLK_CON_GAT_GOUT_USI_CMGP03_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI3_PCLK, "gout_cmgp_usi3_pclk",
	     "gout_cmgp_usi3_qch", CLK_CON_GAT_GOUT_USI_CMGP03_PCLK,
	     21, 0, 0),
};

static const struct samsung_cmu_info cmgp_cmu_info __initconst = {
	.mux_clks		= cmgp_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmgp_mux_clks),
	.div_clks		= cmgp_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmgp_div_clks),
	.gate_clks		= cmgp_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cmgp_gate_clks),
	.nr_clk_ids		= CLKS_NR_CMGP,
	.clk_regs		= cmgp_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmgp_clk_regs),
	.clk_name		= "dout_clkcmu_cmgp_bus",
};

static void __init exynos9810_cmu_top_init(struct device_node *np)
{
	void __iomem *base;
	u32 val;
	int ret;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("exynos9810-cmu-top: failed to map registers\n");
		return;
	}

	val = readl(base + CLK_CON_MUX_MUX_CLKCMU_FSYS1_PCIE);
	if (!(val & BIT(0))) {
		writel(val | BIT(0),
		       base + CLK_CON_MUX_MUX_CLKCMU_FSYS1_PCIE);
		ret = readl_poll_timeout_atomic(
			base + CLK_CON_MUX_MUX_CLKCMU_FSYS1_PCIE,
			val, !(val & BIT(16)), 1, 100);
		if (ret)
			pr_warn("exynos9810-cmu-top: PCIe mux timed out\n");
	}

	iounmap(base);
}

static int __init exynos9810_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	if (info == &top_cmu_info)
		exynos9810_cmu_top_init(dev->of_node);

	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

static const struct of_device_id exynos9810_cmu_of_match[] = {
	{
		.compatible = "samsung,exynos9810-cmu-top",
		.data = &top_cmu_info,
	}, {
		.compatible = "samsung,exynos9810-cmu-dpu",
		.data = &dpu_cmu_info,
	}, {
		.compatible = "samsung,exynos9810-cmu-aud",
		.data = &aud_cmu_info,
	}, {
		.compatible = "samsung,exynos9810-cmu-fsys0",
		.data = &fsys0_cmu_info,
	}, {
		.compatible = "samsung,exynos9810-cmu-fsys1",
		.data = &fsys1_cmu_info,
	}, {
		.compatible = "samsung,exynos9810-cmu-peric0",
		.data = &peric0_cmu_info,
	}, {
		.compatible = "samsung,exynos9810-cmu-cmgp",
		.data = &cmgp_cmu_info,
	}, {
	},
};

static struct platform_driver exynos9810_cmu_driver __refdata = {
	.driver = {
		.name = "exynos9810-cmu",
		.of_match_table = exynos9810_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exynos9810_cmu_probe,
};

static int __init exynos9810_cmu_init(void)
{
	return platform_driver_register(&exynos9810_cmu_driver);
}
core_initcall(exynos9810_cmu_init);
