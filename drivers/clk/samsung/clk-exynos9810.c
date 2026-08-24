// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Mathias Gluszczynski <admin@krazey.de>
 *
 * Common Clock Framework support for the Samsung Exynos9810 SoC.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/samsung,exynos9810.h>

#include "clk.h"
#include "clk-exynos-arm64.h"

#define CLKS_NR_TOP		(CLK_GOUT_TOP_DPU_BUS + 1)
#define CLKS_NR_FSYS0		(CLK_GOUT_FSYS0_USB30DRD_CTRL + 1)
#define CLKS_NR_FSYS1		(CLK_GOUT_FSYS1_PCIE_SLV + 1)
#define CLKS_NR_PERIC0		(CLK_GOUT_PERIC0_USI3_PCLK + 1)
#define CLKS_NR_CMGP		(CLK_GOUT_CMGP_USI3_PCLK + 1)
#define CLKS_NR_DPU		(CLK_GOUT_DPU_SYSMMU_DPUD1_QCH + 1)

/* ---- CMU_TOP ---------------------------------------------------------- */

#define CLK_CON_GAT_GATE_CLKCMU_CMGP_BUS		0x2028
#define CLK_CON_GAT_GATE_CLKCMU_DPU_BUS		0x204c

static const unsigned long top_clk_regs[] __initconst = {
	CLK_CON_GAT_GATE_CLKCMU_CMGP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DPU_BUS,
};

static const struct samsung_gate_clock top_gate_clks[] __initconst = {
	GATE(CLK_GOUT_TOP_CMGP_BUS, "dout_clkcmu_cmgp_bus",
	     "cmgp_bus_bootclk", CLK_CON_GAT_GATE_CLKCMU_CMGP_BUS,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_TOP_DPU_BUS, "dout_clkcmu_dpu_bus",
	     "dpu_bus_bootclk", CLK_CON_GAT_GATE_CLKCMU_DPU_BUS,
	     21, CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info top_cmu_info __initconst = {
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

/* ---- CMU_PERIC0 -------------------------------------------------------- */

#define PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER		0x0100
#define PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER		0x0120
#define CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG		0x1800
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI03			0x1810
#define CLK_CON_GAT_GATE_PERIC0_UART_DBG			0x2008
#define CLK_CON_GAT_GATE_CLK_PERIC0_USI03		0x2018
#define CLK_CON_GAT_GOUT_PERIC0_UART_DBG_RST		0x204c
#define CLK_CON_GAT_GOUT_PERIC0_USI03_RST		0x206c
#define CLK_CON_GAT_GOUT_PERIC0_SYSREG_PCLK		0x2098
#define CLK_CON_GAT_GOUT_PERIC0_UART_DBG_IPCLK		0x209c
#define CLK_CON_GAT_GOUT_PERIC0_UART_DBG_PCLK		0x20a0
#define CLK_CON_GAT_GOUT_PERIC0_USI03_IPCLK		0x20dc
#define CLK_CON_GAT_GOUT_PERIC0_USI03_PCLK		0x20e0
#define QCH_CON_SYSREG_PERIC0				0x3014
#define QCH_CON_UART_DBG					0x3018
#define QCH_CON_USI03					0x3038

static const unsigned long peric0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER,
	CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI03,
	CLK_CON_GAT_GATE_PERIC0_UART_DBG,
	CLK_CON_GAT_GATE_CLK_PERIC0_USI03,
	CLK_CON_GAT_GOUT_PERIC0_UART_DBG_RST,
	CLK_CON_GAT_GOUT_PERIC0_USI03_RST,
	CLK_CON_GAT_GOUT_PERIC0_SYSREG_PCLK,
	CLK_CON_GAT_GOUT_PERIC0_UART_DBG_IPCLK,
	CLK_CON_GAT_GOUT_PERIC0_UART_DBG_PCLK,
	CLK_CON_GAT_GOUT_PERIC0_USI03_IPCLK,
	CLK_CON_GAT_GOUT_PERIC0_USI03_PCLK,
	QCH_CON_SYSREG_PERIC0,
	QCH_CON_UART_DBG,
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

static int __init exynos9810_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
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
