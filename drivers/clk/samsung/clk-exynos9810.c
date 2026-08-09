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

#define CLKS_NR_FSYS0		(CLK_GOUT_FSYS0_UFS_EMBD_UNIPRO + 1)
#define CLKS_NR_PERIC0		(CLK_GOUT_PERIC0_UART_DBG_IPCLK + 1)

/* ---- CMU_FSYS0 --------------------------------------------------------- */

#define PLL_CON0_MUX_CLKCMU_FSYS0_BUS_USER		0x0100
#define PLL_CON0_MUX_CLKCMU_FSYS0_UFS_EMBD_USER		0x0180
#define CLK_CON_GAT_GOUT_FSYS0_UFS_EMBD_UNIPRO		0x205c
#define QCH_CON_UFS_EMBD					0x3044

static const unsigned long fsys0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_FSYS0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_FSYS0_UFS_EMBD_USER,
	CLK_CON_GAT_GOUT_FSYS0_UFS_EMBD_UNIPRO,
	QCH_CON_UFS_EMBD,
};

PNAME(mout_fsys0_bus_user_p) = {
	"oscclk", "dout_clkcmu_fsys0_bus"
};

PNAME(mout_fsys0_ufs_embd_user_p) = {
	"oscclk", "dout_clkcmu_fsys0_ufs_embd"
};

static const struct samsung_mux_clock fsys0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_FSYS0_BUS_USER, "mout_fsys0_bus_user",
	    mout_fsys0_bus_user_p, PLL_CON0_MUX_CLKCMU_FSYS0_BUS_USER,
	    4, 1),
	MUX(CLK_MOUT_FSYS0_UFS_EMBD_USER, "mout_fsys0_ufs_embd_user",
	    mout_fsys0_ufs_embd_user_p,
	    PLL_CON0_MUX_CLKCMU_FSYS0_UFS_EMBD_USER, 4, 1),
};

static const struct samsung_gate_clock fsys0_gate_clks[] __initconst = {
	/* QCH request controls the UFS bus clock when HWACG is disabled. */
	GATE(CLK_GOUT_FSYS0_UFS_EMBD_ACLK,
	     "gout_fsys0_ufs_embd_aclk", "mout_fsys0_bus_user",
	     QCH_CON_UFS_EMBD, 1, 0, 0),
	GATE(CLK_GOUT_FSYS0_UFS_EMBD_UNIPRO,
	     "gout_fsys0_ufs_embd_unipro", "mout_fsys0_ufs_embd_user",
	     CLK_CON_GAT_GOUT_FSYS0_UFS_EMBD_UNIPRO, 21, 0, 0),
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

/* ---- CMU_PERIC0 -------------------------------------------------------- */

#define PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER		0x0100
#define PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER		0x0120
#define CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG		0x1800
#define CLK_CON_GAT_GATE_PERIC0_UART_DBG			0x2008
#define CLK_CON_GAT_GOUT_PERIC0_UART_DBG_RST		0x204c
#define CLK_CON_GAT_GOUT_PERIC0_UART_DBG_IPCLK		0x209c
#define CLK_CON_GAT_GOUT_PERIC0_UART_DBG_PCLK		0x20a0
#define QCH_CON_UART_DBG					0x3018

static const unsigned long peric0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER,
	CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG,
	CLK_CON_GAT_GATE_PERIC0_UART_DBG,
	CLK_CON_GAT_GOUT_PERIC0_UART_DBG_RST,
	CLK_CON_GAT_GOUT_PERIC0_UART_DBG_IPCLK,
	CLK_CON_GAT_GOUT_PERIC0_UART_DBG_PCLK,
	QCH_CON_UART_DBG,
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
 * S-Boot configures the UART divider before entering Linux. Register it
 * without forcing a new rate so the early console keeps its 200 MHz clock.
 */
static const struct samsung_div_clock peric0_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERIC0_UART_DBG, "dout_peric0_uart_dbg",
	    "gout_peric0_uart_dbg", CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG,
	    0, 4),
};

static const struct samsung_gate_clock peric0_gate_clks[] __initconst = {
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
		.compatible = "samsung,exynos9810-cmu-fsys0",
		.data = &fsys0_cmu_info,
	}, {
		.compatible = "samsung,exynos9810-cmu-peric0",
		.data = &peric0_cmu_info,
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
