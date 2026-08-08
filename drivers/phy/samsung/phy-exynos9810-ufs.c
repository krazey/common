// SPDX-License-Identifier: GPL-2.0-only
/*
 * UFS PHY driver data for Samsung Exynos9810 SoC
 *
 * The calibration values and CDR AFC retry sequence are derived from the
 * Exynos9810 vendor kernel and expressed through the generic Samsung UFS PHY
 * framework.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>

#include "phy-samsung-ufs.h"

#define EXYNOS9810_EMBEDDED_COMBO_PHY_CTRL		0x724
#define EXYNOS9810_EMBEDDED_COMBO_PHY_CTRL_MASK		BIT(0)
#define EXYNOS9810_EMBEDDED_COMBO_PHY_CTRL_EN		BIT(0)

#define EXYNOS9810_PHY_TRSV_LANE_OFFSET			0x50
#define EXYNOS9810_PHY_CDR_AFC_STATUS			0x7f
#define EXYNOS9810_PHY_CDR_AFC_LOCK			BIT(6)
#define EXYNOS9810_PHY_CDR_AFC_HIBERN8_LOCK		BIT(0)
#define EXYNOS9810_PHY_CDR_AFC_TOGGLE			0x3c

#define EXYNOS9810_CDR_AFC_DELAY_US			40
#define EXYNOS9810_CDR_AFC_RETRIES			100

#define PHY_TRSV_REG_CFG_9810(o, v, d) \
	PHY_TRSV_REG_CFG_OFFSET(o, v, d, EXYNOS9810_PHY_TRSV_LANE_OFFSET)

static const struct samsung_ufs_phy_cfg exynos9810_pre_init_cfg[] = {
	PHY_COMN_REG_CFG(0x023, 0x80, PWR_MODE_ANY),
	PHY_COMN_REG_CFG(0x01d, 0x10, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x044, 0xb5, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x04d, 0x43, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x05b, 0x20, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x05e, 0xc0, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x038, 0x12, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x059, 0x58, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x06c, 0x18, PWR_MODE_ANY),
	PHY_COMN_REG_CFG(0x023, 0xc0, PWR_MODE_ANY),
	PHY_COMN_REG_CFG(0x023, 0x00, PWR_MODE_ANY),
	END_UFS_PHY_CFG,
};

/* Gear 3, two-lane HS series A/B values from the vendor calibration. */
static const struct samsung_ufs_phy_cfg exynos9810_pre_pwr_hs_cfg[] = {
	PHY_TRSV_REG_CFG_9810(0x032, 0xbc, PWR_MODE_HS_ANY),
	PHY_TRSV_REG_CFG_9810(0x03c, 0x7f, PWR_MODE_HS_ANY),
	PHY_TRSV_REG_CFG_9810(0x048, 0xc0, PWR_MODE_HS_ANY),
	PHY_TRSV_REG_CFG_9810(0x04a, 0x00, PWR_MODE_HS_G3_ANY),
	PHY_TRSV_REG_CFG_9810(0x04b, 0x00, PWR_MODE_HS_G3_ANY),
	PHY_TRSV_REG_CFG_9810(0x04d, 0x63, PWR_MODE_HS_G3_ANY),
	END_UFS_PHY_CFG,
};

static const struct samsung_ufs_phy_cfg exynos9810_post_pwr_hs_cfg[] = {
	END_UFS_PHY_CFG,
};

static const struct samsung_ufs_phy_cfg exynos9810_post_hibern8_enter_cfg[] = {
	PHY_TRSV_REG_CFG_9810(0x031, 0x99, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x03a, 0x7f, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x03c, 0x7f, PWR_MODE_ANY),
	PHY_COMN_REG_CFG(0x001, 0x02, PWR_MODE_ANY),
	END_UFS_PHY_CFG,
};

static const struct samsung_ufs_phy_cfg exynos9810_pre_hibern8_exit_cfg[] = {
	PHY_COMN_REG_CFG(0x001, 0x3f, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x031, 0xd9, PWR_MODE_ANY),
	PHY_TRSV_REG_CFG_9810(0x03a, 0x77, PWR_MODE_ANY),
	END_UFS_PHY_CFG,
};

static int exynos9810_ufs_phy_wait_for_cal(struct phy *phy, u8 lane)
{
	/* The delay is common to the PHY rather than per lane. */
	if (!lane)
		usleep_range(200, 210);

	return 0;
}

static int exynos9810_wait_cdr_afc(struct phy *phy, u8 lane, u32 lock_mask)
{
	struct samsung_ufs_phy *ufs_phy = get_samsung_ufs_phy(phy);
	const struct samsung_ufs_phy_cfg toggle_cfg[] = {
		PHY_TRSV_REG_CFG_9810(EXYNOS9810_PHY_CDR_AFC_TOGGLE,
				      0x7f, PWR_MODE_ANY),
		PHY_TRSV_REG_CFG_9810(EXYNOS9810_PHY_CDR_AFC_TOGGLE,
				      0xff, PWR_MODE_ANY),
		END_UFS_PHY_CFG,
	};
	void __iomem *status;
	u32 reg;
	int i;

	status = ufs_phy->reg_pma +
		 PHY_APB_ADDR(EXYNOS9810_PHY_CDR_AFC_STATUS +
			       EXYNOS9810_PHY_TRSV_LANE_OFFSET * lane);

	for (i = 0; i < EXYNOS9810_CDR_AFC_RETRIES; i++) {
		udelay(EXYNOS9810_CDR_AFC_DELAY_US);

		reg = readl(status);
		if ((reg & lock_mask) == lock_mask)
			return 0;

		udelay(1);
		samsung_ufs_phy_config(ufs_phy, &toggle_cfg[0], lane);
		samsung_ufs_phy_config(ufs_phy, &toggle_cfg[1], lane);
	}

	dev_err(ufs_phy->dev, "failed to get CDR AFC lock on lane %u\n",
		lane);
	return -ETIMEDOUT;
}

static int exynos9810_ufs_phy_wait_cdr_afc(struct phy *phy, u8 lane)
{
	return exynos9810_wait_cdr_afc(phy, lane,
					 EXYNOS9810_PHY_CDR_AFC_LOCK);
}

static int exynos9810_ufs_phy_wait_cdr_afc_hibern8(struct phy *phy, u8 lane)
{
	struct samsung_ufs_phy *ufs_phy = get_samsung_ufs_phy(phy);
	const struct samsung_ufs_phy_cfg release_cfg =
		PHY_TRSV_REG_CFG_9810(EXYNOS9810_PHY_CDR_AFC_TOGGLE,
				      0xff, PWR_MODE_ANY);

	fsleep(10);
	samsung_ufs_phy_config(ufs_phy, &release_cfg, lane);

	return exynos9810_wait_cdr_afc(phy, lane,
					 EXYNOS9810_PHY_CDR_AFC_HIBERN8_LOCK);
}

static const struct samsung_ufs_phy_cfg *exynos9810_ufs_phy_cfgs[CFG_TAG_MAX] = {
	[CFG_PRE_INIT]		= exynos9810_pre_init_cfg,
	[CFG_PRE_PWR_HS]	= exynos9810_pre_pwr_hs_cfg,
	[CFG_POST_PWR_HS]	= exynos9810_post_pwr_hs_cfg,
};

static const struct samsung_ufs_phy_cfg *exynos9810_ufs_phy_hibern8_cfgs[] = {
	[CFG_POST_HIBERN8_ENTER]	= exynos9810_post_hibern8_enter_cfg,
	[CFG_PRE_HIBERN8_EXIT]		= exynos9810_pre_hibern8_exit_cfg,
};

static const char * const exynos9810_ufs_phy_clks[] = {
	"ref_clk",
};

const struct samsung_ufs_phy_drvdata exynos9810_ufs_phy = {
	.cfgs = exynos9810_ufs_phy_cfgs,
	.cfgs_hibern8 = exynos9810_ufs_phy_hibern8_cfgs,
	.isol = {
		.offset = EXYNOS9810_EMBEDDED_COMBO_PHY_CTRL,
		.mask = EXYNOS9810_EMBEDDED_COMBO_PHY_CTRL_MASK,
		.en = EXYNOS9810_EMBEDDED_COMBO_PHY_CTRL_EN,
	},
	.clk_list = exynos9810_ufs_phy_clks,
	.num_clks = ARRAY_SIZE(exynos9810_ufs_phy_clks),
	.cdr_lock_status_offset = EXYNOS9810_PHY_CDR_AFC_STATUS,
	.wait_for_cal = exynos9810_ufs_phy_wait_for_cal,
	.wait_for_cdr = exynos9810_ufs_phy_wait_cdr_afc,
	.wait_for_cdr_hibern8 = exynos9810_ufs_phy_wait_cdr_afc_hibern8,
};
