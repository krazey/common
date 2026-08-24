// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Samsung Exynos SoCs
 *
 * Copyright (C) 2013-2020 Samsung Electronics Co., Ltd.
 *		https://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 *	   Jaehoon Chung <jh80.chung@samsung.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pci-exynos9810.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/sizes.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include "pcie-designware.h"

#define to_exynos_pcie(x)	dev_get_drvdata((x)->dev)

/* PCIe ELBI registers */
#define PCIE_IRQ_PULSE			0x000
#define IRQ_INTA_ASSERT			BIT(0)
#define IRQ_INTB_ASSERT			BIT(2)
#define IRQ_INTC_ASSERT			BIT(4)
#define IRQ_INTD_ASSERT			BIT(6)
#define PCIE_IRQ_LEVEL			0x004
#define PCIE_IRQ_SPECIAL		0x008
#define PCIE_IRQ_EN_PULSE		0x00c
#define PCIE_IRQ_EN_LEVEL		0x010
#define PCIE_IRQ_EN_SPECIAL		0x014
#define PCIE_SW_WAKE			0x018
#define PCIE_BUS_EN			BIT(1)
#define PCIE_CORE_RESET			0x01c
#define PCIE_CORE_RESET_ENABLE		BIT(0)
#define PCIE_STICKY_RESET		0x020
#define PCIE_NONSTICKY_RESET		0x024
#define PCIE_APP_INIT_RESET		0x028
#define PCIE_APP_LTSSM_ENABLE		0x02c
#define PCIE_ELBI_RDLH_LINKUP		0x074
#define PCIE_ELBI_XMLH_LINKUP		BIT(4)
#define PCIE_ELBI_LTSSM_ENABLE		0x1
#define PCIE_ELBI_SLV_AWMISC		0x11c
#define PCIE_ELBI_SLV_ARMISC		0x120
#define PCIE_ELBI_SLV_DBI_ENABLE	BIT(21)

#define EXYNOS9810_PCIE_REQ_EXIT_L1_MODE	0x0f4
#define EXYNOS9810_PCIE_REQ_EXIT_L1	BIT(0)
#define EXYNOS9810_PCIE_L1_NAK_CONTROL	BIT(4)
#define EXYNOS9810_PCIE_LINKDOWN_RESET	0x1b8
#define EXYNOS9810_PCIE_LINKDOWN_MANUAL	BIT(1)
#define EXYNOS9810_PCIE_CORE_RESET	0x1d0
#define EXYNOS9810_PCIE_STATE_HISTORY	0x274
#define EXYNOS9810_PCIE_HISTORY_ENABLE	GENMASK(1, 0)
#define EXYNOS9810_PCIE_PCS_RESET	0x288
#define EXYNOS9810_PCIE_PHY_RESET	0x28c
#define EXYNOS9810_PCIE_MAC_RESET	0x290
#define EXYNOS9810_PCIE_STATE_POWER_S	0x2bc
#define EXYNOS9810_PCIE_STATE_POWER_M	0x2c0
#define EXYNOS9810_PCIE_QCH_SELECT	0x2c8

#define EXYNOS9810_PMU_PCIE_PHY		0x71c
#define EXYNOS9810_PMU_WAKEUP_MASK	0x610
#define EXYNOS9810_PMU_WAKEUP_PCIE_WIFI	BIT(5)
#define EXYNOS9810_SYSREG_PCIE_SHARABILITY	0x700
#define EXYNOS9810_SYSREG_PCIE_CTRL	0x1044
#define EXYNOS9810_SYSREG_PCIE_LANES	0x1050
#define EXYNOS9810_SYSREG_PCIE_SHARABLE	GENMASK(9, 8)

#define EXYNOS9810_PCIE_AUX_CLK_FREQ	0xb40
#define EXYNOS9810_PCIE_L1_SUBSTATES	0xb44
#define EXYNOS9810_PCIE_AUX_CLK_26MHZ	0x1a
#define EXYNOS9810_PCIE_L1_SUB_VAL	0xea
#define EXYNOS9810_PCIE_DEVICE_ID	0xecec

#define EXYNOS9810_PCIE_LINK_RETRIES	10
#define EXYNOS9810_PCIE_LINK_POLLS	2000

struct exynos_pcie_data {
	bool integrated_phy;
	bool preserve_boot_clocks;
};

struct exynos_pcie_reg_value {
	u32 offset;
	u32 value;
};

struct exynos_pcie {
	struct dw_pcie			pci;
	struct clk_bulk_data		*clks;
	struct phy			*phy;
	struct regulator_bulk_data	supplies[2];
	const struct exynos_pcie_data	*data;
	void __iomem			*phy_base;
	void __iomem			*pcs_base;
	void __iomem			*ia_base;
	struct regmap			*pmureg;
	struct regmap			*sysreg;
	struct gpio_desc		*reset_gpio;
	struct pinctrl			*pinctrl;
	struct pinctrl_state		*pins_default;
	struct pinctrl_state		*pins_idle;
	struct regulator		*vpcie3v3;
	bool				phy_initialized;
	bool				vpcie3v3_enabled;
	bool				supplies_enabled;
	bool				link_deferred;
	bool				link_active;
	bool				irq_enabled;
};

static void exynos_pcie_enable_irq_pulse(struct exynos_pcie *ep);
static DEFINE_MUTEX(exynos9810_wlan_pcie_lock);
static struct exynos_pcie *exynos9810_wlan_pcie;

static void exynos_pcie_writel(void __iomem *base, u32 val, u32 reg)
{
	writel(val, base + reg);
}

static u32 exynos_pcie_readl(void __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static const u32 exynos9810_pcie_phy_common[] = {
	0x01, 0xe1, 0x05, 0x00, 0x88, 0x88, 0x88, 0x0c,
	0x61, 0x45, 0x65, 0x24, 0x33, 0x18, 0xe3, 0xfc,
	0xd8, 0x05, 0xe6, 0x80, 0x00, 0x00, 0x00, 0x00,
	0x60, 0x11, 0x00, 0xa0, 0x05, 0x04, 0x18, 0x88,
	0xc0, 0xff, 0x9b, 0x52, 0x22, 0x30, 0x4f, 0xdc,
	0x40, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x80,
};

static const u32 exynos9810_pcie_phy_trsv[] = {
	0x31, 0x40, 0x37, 0x99, 0x85, 0x00, 0xc0, 0xff,
	0xff, 0x3f, 0x8c, 0xc8, 0x02, 0x01, 0x88, 0x80,
	0x06, 0x90, 0x6c, 0x66, 0x09, 0x61, 0x42, 0x44,
	0xc6, 0x50, 0x0a, 0x33, 0x58, 0xe7, 0x20, 0x22,
	0x80, 0x38, 0x05, 0x85, 0x00, 0x00, 0x00, 0x7e,
	0x00, 0x00, 0x55, 0x15, 0xac, 0xaa, 0x3e, 0x00,
	0x00, 0x00, 0x20, 0x3f, 0x00, 0x03, 0x01, 0x00,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
	0x05, 0x85, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const struct exynos_pcie_reg_value exynos9810_pcie_ia_sequence[] = {
	{ 0x010, 0x0000000b },
	{ 0x040, 0x0002ffff },
	{ 0x100, 0x50000004 },
	{ 0x104, 0x0000ffff },
	{ 0x108, 0x20930014 },
	{ 0x10c, 0x0000000a },
	{ 0x110, 0x40030044 },
	{ 0x114, 0x00000001 },
	{ 0x118, 0x50000004 },
	{ 0x11c, 0x00000020 },
	{ 0x120, 0x209101ec },
	{ 0x124, 0x00000020 },
	{ 0x128, 0x400200d0 },
	{ 0x12c, 0x000000c0 },
	{ 0x130, 0x400200d0 },
	{ 0x134, 0x00000080 },
	{ 0x138, 0x400200d0 },
	{ 0x13c, 0x00000000 },
	{ 0x140, 0x100101ec },
	{ 0x144, 0x00000020 },
	{ 0x148, 0x70000004 },
	{ 0x14c, 0x00000010 },
	{ 0x150, 0x40030010 },
	{ 0x154, 0x0001000b },
	{ 0x158, 0x80000000 },
	{ 0x15c, 0x00000000 },
};

static void exynos9810_pcie_set_rx_elecidle(struct exynos_pcie *ep,
					    bool ignore)
{
	u32 val;

	val = exynos_pcie_readl(ep->pcs_base, 0xec);
	if (ignore)
		val |= BIT(3);
	else
		val &= ~BIT(3);
	exynos_pcie_writel(ep->pcs_base, val, 0xec);
}

static void exynos9810_pcie_config_ia(struct exynos_pcie *ep)
{
	unsigned int i;

	exynos_pcie_writel(ep->pci.elbi_base, 0x10, 0x34);
	exynos_pcie_writel(ep->ia_base, 0x116a0000, 0x30);
	exynos_pcie_writel(ep->ia_base, 0x116d0000, 0x34);
	exynos_pcie_writel(ep->ia_base, 0x116c0000, 0x38);
	exynos_pcie_writel(ep->ia_base, 0x11680000, 0x3c);

	for (i = 0; i < ARRAY_SIZE(exynos9810_pcie_ia_sequence); i++)
		exynos_pcie_writel(ep->ia_base,
				   exynos9810_pcie_ia_sequence[i].value,
				   exynos9810_pcie_ia_sequence[i].offset);

	exynos_pcie_writel(ep->ia_base, 1, 0);
}

static void exynos9810_pcie_toggle_reset(struct exynos_pcie *ep, u32 reg,
					 unsigned int final_delay)
{
	void __iomem *elbi = ep->pci.elbi_base;

	exynos_pcie_writel(elbi, 1, reg);
	fsleep(10);
	exynos_pcie_writel(elbi, 0, reg);
	fsleep(10);
	exynos_pcie_writel(elbi, 1, reg);
	fsleep(final_delay);
}

static void exynos9810_pcie_phy_power_on(struct exynos_pcie *ep)
{
	u32 val;

	val = exynos_pcie_readl(ep->pcs_base, 0x100);
	exynos_pcie_writel(ep->pcs_base, val & ~BIT(6), 0x100);

	val = exynos_pcie_readl(ep->pcs_base, 0x104);
	exynos_pcie_writel(ep->pcs_base, val & ~BIT(7), 0x104);

	exynos_pcie_writel(ep->phy_base, 0xc0, 0x20 * 4);
	exynos_pcie_writel(ep->phy_base, 0x7e, 0x57 * 4);
}

static void exynos9810_pcie_phy_power_down(struct exynos_pcie *ep)
{
	u32 val;

	exynos_pcie_writel(ep->phy_base, 0xfe, 0x57 * 4);
	exynos_pcie_writel(ep->phy_base, 0xc1, 0x20 * 4);

	val = exynos_pcie_readl(ep->pcs_base, 0x100);
	exynos_pcie_writel(ep->pcs_base, val | BIT(6), 0x100);

	val = exynos_pcie_readl(ep->pcs_base, 0x104);
	exynos_pcie_writel(ep->pcs_base, val | BIT(7), 0x104);
}

static int exynos9810_pcie_prepare_power_on(struct exynos_pcie *ep)
{
	int ret;

	ret = regmap_update_bits(ep->pmureg, EXYNOS9810_PMU_PCIE_PHY,
				 BIT(0), BIT(0));
	if (ret)
		return ret;

	exynos9810_pcie_phy_power_on(ep);

	return 0;
}

static void exynos9810_pcie_enable_history(struct exynos_pcie *ep)
{
	void __iomem *elbi = ep->pci.elbi_base;

	exynos_pcie_writel(elbi, 0, EXYNOS9810_PCIE_STATE_HISTORY);
	exynos_pcie_writel(elbi, EXYNOS9810_PCIE_HISTORY_ENABLE,
			   EXYNOS9810_PCIE_STATE_HISTORY);
	exynos_pcie_writel(elbi, 0x200000,
			   EXYNOS9810_PCIE_STATE_POWER_S);
	exynos_pcie_writel(elbi, U32_MAX,
			   EXYNOS9810_PCIE_STATE_POWER_M);
}

static void exynos9810_pcie_disable_history(struct exynos_pcie *ep)
{
	void __iomem *elbi = ep->pci.elbi_base;
	u32 val;

	val = exynos_pcie_readl(elbi, EXYNOS9810_PCIE_STATE_HISTORY);
	val &= ~EXYNOS9810_PCIE_HISTORY_ENABLE;
	exynos_pcie_writel(elbi, val, EXYNOS9810_PCIE_STATE_HISTORY);
}

static void exynos9810_pcie_log_power_state(struct exynos_pcie *ep)
{
	void __iomem *elbi = ep->pci.elbi_base;
	int perst;
	int perst_raw;
	int rail;

	rail = regulator_is_enabled(ep->vpcie3v3);
	perst = gpiod_get_value_cansleep(ep->reset_gpio);
	perst_raw = gpiod_get_raw_value_cansleep(ep->reset_gpio);

	dev_info(ep->pci.dev,
		 "E981D: WLAN PCIe controls rail=%d perst=%d/raw=%d\n",
		 rail, perst, perst_raw);
	dev_info(ep->pci.dev,
		 "E981D: WLAN PCIe ELBI phy=%#x/%#x hist=%#x pwr=%#x/%#x\n",
		 exynos_pcie_readl(ep->phy_base, 0x20 * 4),
		 exynos_pcie_readl(ep->phy_base, 0x57 * 4),
		 exynos_pcie_readl(elbi, EXYNOS9810_PCIE_STATE_HISTORY),
		 exynos_pcie_readl(elbi, EXYNOS9810_PCIE_STATE_POWER_S),
		 exynos_pcie_readl(elbi, EXYNOS9810_PCIE_STATE_POWER_M));
}

static int exynos9810_pcie_phy_init(struct exynos_pcie *ep)
{
	u32 val;
	unsigned int i;
	int ret;

	ret = regmap_update_bits(ep->sysreg, EXYNOS9810_SYSREG_PCIE_CTRL,
				 BIT(1), BIT(1));
	if (ret)
		return ret;

	ret = regmap_update_bits(ep->sysreg, EXYNOS9810_SYSREG_PCIE_LANES,
				 GENMASK(7, 1) | BIT(12),
				 GENMASK(3, 2) | BIT(12));
	if (ret)
		return ret;

	exynos9810_pcie_toggle_reset(ep, EXYNOS9810_PCIE_PCS_RESET, 10);

	for (i = 0; i < ARRAY_SIZE(exynos9810_pcie_phy_common); i++)
		exynos_pcie_writel(ep->phy_base,
				   exynos9810_pcie_phy_common[i], i * 4);

	for (i = 0; i < ARRAY_SIZE(exynos9810_pcie_phy_trsv); i++)
		exynos_pcie_writel(ep->phy_base,
				   exynos9810_pcie_phy_trsv[i],
				   (0x30 + i) * 4);

	exynos_pcie_writel(ep->pcs_base, 0x70, 0xf8);
	exynos_pcie_writel(ep->pcs_base, 0x87, 0x100);
	exynos_pcie_writel(ep->pcs_base, 0x50, 0x104);

	val = exynos_pcie_readl(ep->pcs_base, 0x0c);
	val &= ~BIT(1);
	val |= BIT(4);
	exynos_pcie_writel(ep->pcs_base, val, 0x0c);

	exynos9810_pcie_toggle_reset(ep, EXYNOS9810_PCIE_MAC_RESET, 10);
	exynos9810_pcie_toggle_reset(ep, EXYNOS9810_PCIE_PHY_RESET, 100);

	val = exynos_pcie_readl(ep->pcs_base, 0xd0);
	val |= GENMASK(7, 6);
	exynos_pcie_writel(ep->pcs_base, val, 0xd0);
	fsleep(20);

	val &= ~GENMASK(7, 6);
	val |= BIT(7);
	exynos_pcie_writel(ep->pcs_base, val, 0xd0);
	val &= ~GENMASK(7, 6);
	exynos_pcie_writel(ep->pcs_base, val, 0xd0);

	return 0;
}

static int exynos9810_pcie_initial_powerdown(struct exynos_pcie *ep)
{
	struct device *dev = ep->pci.dev;
	int ret;

	gpiod_set_value_cansleep(ep->reset_gpio, 1);
	exynos_pcie_writel(ep->pci.elbi_base, 0, PCIE_APP_LTSSM_ENABLE);

	ret = pinctrl_select_state(ep->pinctrl, ep->pins_idle);
	if (ret)
		return ret;

	ret = regmap_update_bits(ep->pmureg, EXYNOS9810_PMU_PCIE_PHY,
				 BIT(0), BIT(0));
	if (ret)
		return ret;

	ret = regmap_update_bits(ep->sysreg,
				 EXYNOS9810_SYSREG_PCIE_SHARABILITY,
				 EXYNOS9810_SYSREG_PCIE_SHARABLE,
				 EXYNOS9810_SYSREG_PCIE_SHARABLE);
	if (ret)
		return ret;

	ret = exynos9810_pcie_phy_init(ep);
	if (ret)
		return ret;

	exynos9810_pcie_config_ia(ep);
	exynos9810_pcie_phy_power_down(ep);

	ret = regmap_update_bits(ep->pmureg, EXYNOS9810_PMU_WAKEUP_MASK,
				 EXYNOS9810_PMU_WAKEUP_PCIE_WIFI,
				 EXYNOS9810_PMU_WAKEUP_PCIE_WIFI);
	if (ret)
		return ret;

	dev_info(dev, "WLAN link held in initial power-down state\n");

	return 0;
}

static int exynos9810_pcie_host_init(struct exynos_pcie *ep)
{
	void __iomem *elbi = ep->pci.elbi_base;
	u32 val;
	int ret;

	exynos9810_pcie_set_rx_elecidle(ep, true);

	exynos_pcie_writel(elbi, 0, EXYNOS9810_PCIE_CORE_RESET);
	fsleep(20);
	exynos_pcie_writel(elbi, 1, EXYNOS9810_PCIE_CORE_RESET);

	gpiod_set_value_cansleep(ep->reset_gpio, 0);
	usleep_range(18000, 20000);

	val = exynos_pcie_readl(elbi, EXYNOS9810_PCIE_REQ_EXIT_L1_MODE);
	val |= EXYNOS9810_PCIE_REQ_EXIT_L1;
	val |= EXYNOS9810_PCIE_L1_NAK_CONTROL;
	exynos_pcie_writel(elbi, val, EXYNOS9810_PCIE_REQ_EXIT_L1_MODE);
	exynos_pcie_writel(elbi, EXYNOS9810_PCIE_LINKDOWN_MANUAL,
			   EXYNOS9810_PCIE_LINKDOWN_RESET);

	val = exynos_pcie_readl(elbi, EXYNOS9810_PCIE_QCH_SELECT);
	val &= ~GENMASK(11, 0);
	exynos_pcie_writel(elbi, val, EXYNOS9810_PCIE_QCH_SELECT);

	ret = exynos9810_pcie_phy_init(ep);
	if (ret) {
		gpiod_set_value_cansleep(ep->reset_gpio, 1);
		return ret;
	}

	exynos9810_pcie_config_ia(ep);

	val = exynos_pcie_readl(elbi, PCIE_SW_WAKE);
	val &= ~PCIE_BUS_EN;
	exynos_pcie_writel(elbi, val, PCIE_SW_WAKE);

	return 0;
}

static void exynos9810_pcie_link_power_down(struct exynos_pcie *ep)
{
	bool was_powered = ep->link_active || ep->vpcie3v3_enabled;

	if (ep->irq_enabled) {
		disable_irq(ep->pci.pp.irq);
		ep->irq_enabled = false;
	}

	exynos9810_pcie_disable_history(ep);
	gpiod_set_value_cansleep(ep->reset_gpio, 1);
	exynos_pcie_writel(ep->pci.elbi_base, 0, PCIE_APP_LTSSM_ENABLE);
	exynos9810_pcie_phy_power_down(ep);
	regmap_update_bits(ep->pmureg, EXYNOS9810_PMU_WAKEUP_MASK,
			   EXYNOS9810_PMU_WAKEUP_PCIE_WIFI,
			   EXYNOS9810_PMU_WAKEUP_PCIE_WIFI);
	pinctrl_select_state(ep->pinctrl, ep->pins_idle);

	if (ep->vpcie3v3_enabled) {
		regulator_disable(ep->vpcie3v3);
		ep->vpcie3v3_enabled = false;
	}

	ep->link_active = false;
	ep->link_deferred = true;

	if (was_powered)
		dev_info(ep->pci.dev, "WLAN PCIe link powered down\n");
}

static void exynos9810_pcie_host_deinit(struct exynos_pcie *ep)
{
	void __iomem *elbi = ep->pci.elbi_base;

	exynos9810_pcie_link_power_down(ep);
	gpiod_set_value_cansleep(ep->reset_gpio, 1);

	if (elbi) {
		exynos_pcie_writel(elbi, 0, PCIE_APP_LTSSM_ENABLE);
		exynos_pcie_writel(elbi, 0, EXYNOS9810_PCIE_CORE_RESET);
	}

	regmap_update_bits(ep->pmureg, EXYNOS9810_PMU_PCIE_PHY, BIT(0), 0);
}

static void exynos_pcie_sideband_dbi_w_mode(struct exynos_pcie *ep, bool on)
{
	struct dw_pcie *pci = &ep->pci;
	u32 val;

	val = exynos_pcie_readl(pci->elbi_base, PCIE_ELBI_SLV_AWMISC);
	if (on)
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
	exynos_pcie_writel(pci->elbi_base, val, PCIE_ELBI_SLV_AWMISC);
}

static void exynos_pcie_sideband_dbi_r_mode(struct exynos_pcie *ep, bool on)
{
	struct dw_pcie *pci = &ep->pci;
	u32 val;

	val = exynos_pcie_readl(pci->elbi_base, PCIE_ELBI_SLV_ARMISC);
	if (on)
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
	exynos_pcie_writel(pci->elbi_base, val, PCIE_ELBI_SLV_ARMISC);
}

static void exynos_pcie_assert_core_reset(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = &ep->pci;
	u32 val;

	val = exynos_pcie_readl(pci->elbi_base, PCIE_CORE_RESET);
	val &= ~PCIE_CORE_RESET_ENABLE;
	exynos_pcie_writel(pci->elbi_base, val, PCIE_CORE_RESET);
	exynos_pcie_writel(pci->elbi_base, 0, PCIE_STICKY_RESET);
	exynos_pcie_writel(pci->elbi_base, 0, PCIE_NONSTICKY_RESET);
}

static void exynos_pcie_deassert_core_reset(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = &ep->pci;
	u32 val;

	val = exynos_pcie_readl(pci->elbi_base, PCIE_CORE_RESET);
	val |= PCIE_CORE_RESET_ENABLE;

	exynos_pcie_writel(pci->elbi_base, val, PCIE_CORE_RESET);
	exynos_pcie_writel(pci->elbi_base, 1, PCIE_STICKY_RESET);
	exynos_pcie_writel(pci->elbi_base, 1, PCIE_NONSTICKY_RESET);
	exynos_pcie_writel(pci->elbi_base, 1, PCIE_APP_INIT_RESET);
	exynos_pcie_writel(pci->elbi_base, 0, PCIE_APP_INIT_RESET);
}

static void exynos9810_pcie_setup_rc(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = &ep->pci;
	u8 pcie_cap;
	u8 pm_cap;
	u32 val;
	u16 val16;

	pcie_cap = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	pm_cap = dw_pcie_find_capability(pci, PCI_CAP_ID_PM);

	dw_pcie_dbi_ro_wr_en(pci);

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_LINK_CONTROL);
	val &= ~(PORT_LINK_MODE_MASK | PORT_LINK_DLL_LINK_EN |
		 PORT_LINK_FAST_LINK_MODE);
	val |= PORT_LINK_MODE_1_LANES;
	dw_pcie_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, val);

	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val &= ~PORT_LOGIC_LINK_WIDTH_MASK;
	val |= PORT_LOGIC_LINK_WIDTH_1_LANES;
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	dw_pcie_writew_dbi(pci, PCI_VENDOR_ID, PCI_VENDOR_ID_SAMSUNG);
	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID,
			   EXYNOS9810_PCIE_DEVICE_ID);

	if (pcie_cap) {
		val = dw_pcie_readl_dbi(pci, pcie_cap + PCI_EXP_LNKCAP);
		val &= ~(PCI_EXP_LNKCAP_L1EL | PCI_EXP_LNKCAP_MLW |
			 PCI_EXP_LNKCAP_SLS);
		val |= (0x7 << 15) | BIT(4);
		val |= pci->max_link_speed & PCI_EXP_LNKCAP_SLS;
		dw_pcie_writel_dbi(pci, pcie_cap + PCI_EXP_LNKCAP, val);
	}

	dw_pcie_writel_dbi(pci, EXYNOS9810_PCIE_AUX_CLK_FREQ,
			   EXYNOS9810_PCIE_AUX_CLK_26MHZ);
	dw_pcie_writel_dbi(pci, EXYNOS9810_PCIE_L1_SUBSTATES,
			   EXYNOS9810_PCIE_L1_SUB_VAL);

	if (pm_cap)
		dw_pcie_writew_dbi(pci, pm_cap + PCI_PM_CTRL, 0);

	if (pcie_cap) {
		val16 = dw_pcie_readw_dbi(pci,
					  pcie_cap + PCI_EXP_LNKCTL);
		val16 |= PCI_EXP_LNKCTL_RL;
		dw_pcie_writew_dbi(pci, pcie_cap + PCI_EXP_LNKCTL,
				   val16);

		val16 = dw_pcie_readw_dbi(pci,
					  pcie_cap + PCI_EXP_LNKCTL2);
		val16 &= ~PCI_EXP_LNKCTL2_TLS;
		val16 |= pci->max_link_speed & PCI_EXP_LNKCTL2_TLS;
		dw_pcie_writew_dbi(pci, pcie_cap + PCI_EXP_LNKCTL2,
				   val16);
	}

}

static int exynos9810_pcie_start_link(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = &ep->pci;
	unsigned int attempt;
	unsigned int count;
	u32 val;
	int ret;

	for (attempt = 0; attempt < EXYNOS9810_PCIE_LINK_RETRIES;
	     attempt++) {
		if (attempt) {
			gpiod_set_value_cansleep(ep->reset_gpio, 1);
			exynos_pcie_writel(pci->elbi_base, 0,
					   PCIE_APP_LTSSM_ENABLE);

			ret = exynos9810_pcie_host_init(ep);
			if (ret)
				return ret;

			ret = dw_pcie_setup_rc(&pci->pp);
			if (ret)
				return ret;
		}

		exynos9810_pcie_setup_rc(ep);
		exynos9810_pcie_set_rx_elecidle(ep, false);

		exynos_pcie_writel(pci->elbi_base,
				   PCIE_ELBI_LTSSM_ENABLE,
				   PCIE_APP_LTSSM_ENABLE);

		for (count = 0; count < EXYNOS9810_PCIE_LINK_POLLS;
		     count++) {
			val = exynos_pcie_readl(pci->elbi_base,
						PCIE_ELBI_RDLH_LINKUP);
			val &= GENMASK(4, 0);
			if (val >= 0x0d && val <= 0x14) {
				exynos_pcie_enable_irq_pulse(ep);
				dev_info(pci->dev,
					 "link trained on attempt %u\n",
					 attempt + 1);
				return 0;
			}
			fsleep(10);
		}

		dev_warn(pci->dev,
			 "link attempt %u failed, LTSSM=0x%x\n",
			 attempt + 1, val);
	}

	return -ETIMEDOUT;
}

static int exynos9810_pcie_wlan_power_on(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = &ep->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	int ret;

	if (ep->link_active && dw_pcie_link_up(pci))
		return 0;

	dev_info(pci->dev, "WLAN PCIe power-on begin\n");

	if (!ep->vpcie3v3_enabled) {
		ret = regulator_enable(ep->vpcie3v3);
		if (ret)
			return ret;
		ep->vpcie3v3_enabled = true;
	}

	ret = pinctrl_select_state(ep->pinctrl, ep->pins_default);
	if (ret)
		goto err_power_down;

	ret = regmap_update_bits(ep->pmureg, EXYNOS9810_PMU_WAKEUP_MASK,
				 EXYNOS9810_PMU_WAKEUP_PCIE_WIFI, 0);
	if (ret)
		goto err_power_down;

	ret = exynos9810_pcie_prepare_power_on(ep);
	if (ret)
		goto err_power_down;

	exynos9810_pcie_enable_history(ep);

	if (!ep->irq_enabled) {
		enable_irq(pp->irq);
		ep->irq_enabled = true;
	}

	ep->link_deferred = false;

	ret = exynos9810_pcie_host_init(ep);
	if (ret)
		goto err_power_down;

	ret = dw_pcie_setup_rc(pp);
	if (ret)
		goto err_power_down;

	exynos9810_pcie_log_power_state(ep);

	ret = exynos9810_pcie_start_link(ep);
	if (ret)
		goto err_power_down;

	ret = dw_pcie_wait_for_link(pci);
	if (ret)
		goto err_power_down;

	ep->link_active = true;

	pci_lock_rescan_remove();
	pci_rescan_bus(pp->bridge->bus);
	pci_unlock_rescan_remove();

	dev_info(pci->dev, "WLAN PCIe link active and bus rescanned\n");

	return 0;

err_power_down:
	dev_err(pci->dev, "WLAN PCIe power-on failed: %d\n", ret);
	exynos9810_pcie_link_power_down(ep);
	return ret;
}

int exynos9810_pcie_wlan_power(bool on)
{
	struct exynos_pcie *ep;
	int ret;

	mutex_lock(&exynos9810_wlan_pcie_lock);
	ep = exynos9810_wlan_pcie;
	if (!ep)
		ret = -EPROBE_DEFER;
	else if (on)
		ret = exynos9810_pcie_wlan_power_on(ep);
	else {
		exynos9810_pcie_link_power_down(ep);
		ret = 0;
	}
	mutex_unlock(&exynos9810_wlan_pcie_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(exynos9810_pcie_wlan_power);

static int exynos_pcie_start_link(struct dw_pcie *pci)
{
	struct exynos_pcie *ep = to_exynos_pcie(pci);
	u32 val;

	if (ep->data->integrated_phy) {
		if (ep->link_deferred)
			return 0;

		return exynos9810_pcie_start_link(ep);
	}

	val = exynos_pcie_readl(pci->elbi_base, PCIE_SW_WAKE);
	val &= ~PCIE_BUS_EN;
	exynos_pcie_writel(pci->elbi_base, val, PCIE_SW_WAKE);

	/* assert LTSSM enable */
	exynos_pcie_writel(pci->elbi_base, PCIE_ELBI_LTSSM_ENABLE,
			  PCIE_APP_LTSSM_ENABLE);
	return 0;
}

static void exynos_pcie_clear_irq_pulse(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = &ep->pci;

	u32 val = exynos_pcie_readl(pci->elbi_base, PCIE_IRQ_PULSE);

	exynos_pcie_writel(pci->elbi_base, val, PCIE_IRQ_PULSE);
}

static irqreturn_t exynos_pcie_irq_handler(int irq, void *arg)
{
	struct exynos_pcie *ep = arg;

	exynos_pcie_clear_irq_pulse(ep);
	return IRQ_HANDLED;
}

static void exynos_pcie_enable_irq_pulse(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = &ep->pci;

	u32 val = IRQ_INTA_ASSERT | IRQ_INTB_ASSERT |
		  IRQ_INTC_ASSERT | IRQ_INTD_ASSERT;

	exynos_pcie_writel(pci->elbi_base, val, PCIE_IRQ_EN_PULSE);
	exynos_pcie_writel(pci->elbi_base, 0, PCIE_IRQ_EN_LEVEL);
	exynos_pcie_writel(pci->elbi_base, 0, PCIE_IRQ_EN_SPECIAL);
}

static u32 exynos_pcie_read_dbi(struct dw_pcie *pci, void __iomem *base,
				u32 reg, size_t size)
{
	struct exynos_pcie *ep = to_exynos_pcie(pci);
	u32 val;

	exynos_pcie_sideband_dbi_r_mode(ep, true);
	dw_pcie_read(base + reg, size, &val);
	exynos_pcie_sideband_dbi_r_mode(ep, false);
	return val;
}

static void exynos_pcie_write_dbi(struct dw_pcie *pci, void __iomem *base,
				  u32 reg, size_t size, u32 val)
{
	struct exynos_pcie *ep = to_exynos_pcie(pci);

	exynos_pcie_sideband_dbi_w_mode(ep, true);
	dw_pcie_write(base + reg, size, val);
	exynos_pcie_sideband_dbi_w_mode(ep, false);
}

static int exynos_pcie_rd_own_conf(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*val = dw_pcie_read_dbi(pci, where, size);
	return PCIBIOS_SUCCESSFUL;
}

static int exynos_pcie_wr_own_conf(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	dw_pcie_write_dbi(pci, where, size, val);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops exynos_pci_ops = {
	.read = exynos_pcie_rd_own_conf,
	.write = exynos_pcie_wr_own_conf,
};

static enum dw_pcie_ltssm exynos_pcie_get_ltssm(struct dw_pcie *pci)
{
	struct exynos_pcie *ep = to_exynos_pcie(pci);
	u32 val;

	if (ep->data->integrated_phy) {
		if (ep->link_deferred)
			return DW_PCIE_LTSSM_DETECT_QUIET;

		val = exynos_pcie_readl(pci->elbi_base,
					PCIE_ELBI_RDLH_LINKUP);
		return val & GENMASK(4, 0);
	}

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_DEBUG0);
	return FIELD_GET(PORT_LOGIC_LTSSM_STATE_MASK, val);
}

static bool exynos_pcie_link_up(struct dw_pcie *pci)
{
	struct exynos_pcie *ep = to_exynos_pcie(pci);
	u32 val = exynos_pcie_readl(pci->elbi_base, PCIE_ELBI_RDLH_LINKUP);

	if (ep->data->integrated_phy) {
		if (ep->link_deferred)
			return false;

		val &= GENMASK(4, 0);
		return val >= 0x0d && val <= 0x14;
	}

	return val & PCIE_ELBI_XMLH_LINKUP;
}

static int exynos_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct exynos_pcie *ep = to_exynos_pcie(pci);
	int ret;

	pp->bridge->ops = &exynos_pci_ops;

	if (ep->data->integrated_phy) {
		if (ep->link_deferred)
			ret = exynos9810_pcie_initial_powerdown(ep);
		else
			ret = exynos9810_pcie_host_init(ep);
		if (ret)
			return ret;

		exynos_pcie_enable_irq_pulse(ep);
		return 0;
	}

	exynos_pcie_assert_core_reset(ep);

	ret = phy_init(ep->phy);
	if (ret)
		return ret;

	ret = phy_power_on(ep->phy);
	if (ret) {
		phy_exit(ep->phy);
		return ret;
	}
	ep->phy_initialized = true;

	exynos_pcie_deassert_core_reset(ep);
	exynos_pcie_enable_irq_pulse(ep);

	return 0;
}

static const struct dw_pcie_host_ops exynos_pcie_host_ops = {
	.init = exynos_pcie_host_init,
};

static int exynos_add_pcie_port(struct exynos_pcie *ep,
				       struct platform_device *pdev)
{
	struct dw_pcie *pci = &ep->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 0);
	if (pp->irq < 0)
		return pp->irq;

	ret = devm_request_irq(dev, pp->irq, exynos_pcie_irq_handler,
			       IRQF_SHARED, "exynos-pcie", ep);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}
	ep->irq_enabled = true;

	pp->ops = &exynos_pcie_host_ops;
	pp->msi_irq[0] = -ENODEV;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	if (ep->data->integrated_phy) {
		disable_irq(pp->irq);
		ep->irq_enabled = false;
	}

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.read_dbi = exynos_pcie_read_dbi,
	.write_dbi = exynos_pcie_write_dbi,
	.link_up = exynos_pcie_link_up,
	.get_ltssm = exynos_pcie_get_ltssm,
	.start_link = exynos_pcie_start_link,
};

static const struct exynos_pcie_data exynos5433_pcie_data;

static const struct exynos_pcie_data exynos9810_pcie_data = {
	.integrated_phy = true,
	.preserve_boot_clocks = true,
};

static int exynos9810_pcie_get_resources(struct exynos_pcie *ep,
					 struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *phy;

	phy = devm_platform_ioremap_resource_byname(pdev, "phy");
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	ep->ia_base = devm_platform_ioremap_resource_byname(pdev, "ia");
	if (IS_ERR(ep->ia_base))
		return PTR_ERR(ep->ia_base);

	ep->pcs_base = phy;
	ep->phy_base = phy + SZ_4K;

	ep->sysreg = syscon_regmap_lookup_by_phandle(np,
						     "samsung,fsys-sysreg");
	if (IS_ERR(ep->sysreg))
		return PTR_ERR(ep->sysreg);

	ep->pmureg = syscon_regmap_lookup_by_phandle(np,
						     "samsung,pmu-syscon");
	if (IS_ERR(ep->pmureg))
		return PTR_ERR(ep->pmureg);

	ep->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ep->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ep->reset_gpio),
				     "failed to get endpoint reset GPIO\n");

	ep->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(ep->pinctrl))
		return dev_err_probe(dev, PTR_ERR(ep->pinctrl),
				     "failed to get PCIe pinctrl\n");

	ep->pins_default = pinctrl_lookup_state(ep->pinctrl,
						PINCTRL_STATE_DEFAULT);
	if (IS_ERR(ep->pins_default))
		return dev_err_probe(dev, PTR_ERR(ep->pins_default),
				     "failed to get PCIe default pins\n");

	ep->pins_idle = pinctrl_lookup_state(ep->pinctrl, PINCTRL_STATE_IDLE);
	if (IS_ERR(ep->pins_idle))
		return dev_err_probe(dev, PTR_ERR(ep->pins_idle),
				     "failed to get PCIe idle pins\n");

	ep->vpcie3v3 = devm_regulator_get(dev, "vpcie3v3");
	if (IS_ERR(ep->vpcie3v3))
		return dev_err_probe(dev, PTR_ERR(ep->vpcie3v3),
				     "failed to get endpoint supply\n");

	return 0;
}

static int exynos5433_pcie_get_resources(struct exynos_pcie *ep,
					 struct device *dev)
{
	ep->phy = devm_of_phy_get(dev, dev->of_node, NULL);
	if (IS_ERR(ep->phy))
		return PTR_ERR(ep->phy);

	ep->supplies[0].supply = "vdd18";
	ep->supplies[1].supply = "vdd10";
	return devm_regulator_bulk_get(dev, ARRAY_SIZE(ep->supplies),
				       ep->supplies);
}

static int exynos_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_pcie *ep;
	int ret;

	ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	ep->data = of_device_get_match_data(dev);
	if (!ep->data)
		return -EINVAL;

	ep->pci.dev = dev;
	ep->pci.ops = &dw_pcie_ops;
	platform_set_drvdata(pdev, ep);
	if (ep->data->integrated_phy)
		ep->link_deferred = true;

	if (!ep->data->preserve_boot_clocks) {
		ret = devm_clk_bulk_get_all_enabled(dev, &ep->clks);
		if (ret < 0)
			return ret;
	}

	if (ep->data->integrated_phy)
		ret = exynos9810_pcie_get_resources(ep, pdev);
	else
		ret = exynos5433_pcie_get_resources(ep, dev);
	if (ret)
		return ret;

	if (!ep->data->integrated_phy) {
		ret = regulator_bulk_enable(ARRAY_SIZE(ep->supplies),
					    ep->supplies);
		if (ret)
			return ret;
		ep->supplies_enabled = true;
	}

	ret = exynos_add_pcie_port(ep, pdev);
	if (ret < 0)
		goto fail_probe;

	if (ep->data->integrated_phy) {
		mutex_lock(&exynos9810_wlan_pcie_lock);
		exynos9810_wlan_pcie = ep;
		mutex_unlock(&exynos9810_wlan_pcie_lock);
	}

	return 0;

fail_probe:
	if (ep->data->integrated_phy) {
		exynos9810_pcie_host_deinit(ep);
		return ret;
	}

	if (ep->phy_initialized) {
		phy_power_off(ep->phy);
		phy_exit(ep->phy);
	}
	if (ep->supplies_enabled)
		regulator_bulk_disable(ARRAY_SIZE(ep->supplies), ep->supplies);

	return ret;
}

static void exynos_pcie_remove(struct platform_device *pdev)
{
	struct exynos_pcie *ep = platform_get_drvdata(pdev);

	if (ep->data->integrated_phy) {
		mutex_lock(&exynos9810_wlan_pcie_lock);
		exynos9810_wlan_pcie = NULL;
		mutex_unlock(&exynos9810_wlan_pcie_lock);
	}

	dw_pcie_host_deinit(&ep->pci.pp);

	if (ep->data->integrated_phy) {
		exynos9810_pcie_host_deinit(ep);
		return;
	}

	exynos_pcie_assert_core_reset(ep);
	if (ep->phy_initialized) {
		phy_power_off(ep->phy);
		phy_exit(ep->phy);
	}
	if (ep->supplies_enabled)
		regulator_bulk_disable(ARRAY_SIZE(ep->supplies), ep->supplies);
}

static int exynos_pcie_suspend_noirq(struct device *dev)
{
	struct exynos_pcie *ep = dev_get_drvdata(dev);

	if (ep->data->integrated_phy)
		return 0;

	exynos_pcie_assert_core_reset(ep);
	phy_power_off(ep->phy);
	phy_exit(ep->phy);
	ep->phy_initialized = false;
	regulator_bulk_disable(ARRAY_SIZE(ep->supplies), ep->supplies);
	ep->supplies_enabled = false;

	return 0;
}

static int exynos_pcie_resume_noirq(struct device *dev)
{
	struct exynos_pcie *ep = dev_get_drvdata(dev);
	struct dw_pcie *pci = &ep->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	int ret;

	if (ep->data->integrated_phy)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ep->supplies), ep->supplies);
	if (ret)
		return ret;
	ep->supplies_enabled = true;

	/* exynos_pcie_host_init controls ep->phy */
	ret = exynos_pcie_host_init(pp);
	if (ret)
		goto disable_supplies;

	dw_pcie_setup_rc(pp);
	exynos_pcie_start_link(pci);
	ret = dw_pcie_wait_for_link(pci);
	if (!ret)
		return 0;

	phy_power_off(ep->phy);
	phy_exit(ep->phy);
	ep->phy_initialized = false;

disable_supplies:
	regulator_bulk_disable(ARRAY_SIZE(ep->supplies), ep->supplies);
	ep->supplies_enabled = false;
	return ret;
}

static const struct dev_pm_ops exynos_pcie_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(exynos_pcie_suspend_noirq,
				  exynos_pcie_resume_noirq)
};

static const struct of_device_id exynos_pcie_of_match[] = {
	{ .compatible = "samsung,exynos5433-pcie",
	  .data = &exynos5433_pcie_data },
	{ .compatible = "samsung,exynos9810-pcie",
	  .data = &exynos9810_pcie_data },
	{ },
};

static struct platform_driver exynos_pcie_driver = {
	.probe		= exynos_pcie_probe,
	.remove		= exynos_pcie_remove,
	.driver = {
		.name	= "exynos-pcie",
		.of_match_table = exynos_pcie_of_match,
		.pm		= &exynos_pcie_pm_ops,
	},
};
module_platform_driver(exynos_pcie_driver);
MODULE_DESCRIPTION("Samsung Exynos PCIe host controller driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, exynos_pcie_of_match);
