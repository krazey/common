// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Mathias Gluszczynski <admin@krazey.de>
 *
 * ACPM DVFS clocks for the Samsung Exynos9810 SoC.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/samsung,exynos9810.h>
#include <soc/samsung/acpm_ipc_ctrl.h>

#define EXYNOS9810_ACPM_FREQ_SET		0
#define EXYNOS9810_ACPM_COMMAND_WORDS		4

struct exynos9810_acpm_clk {
	struct clk_hw hw;
	unsigned int channel;
	u32 id;
	unsigned long rate;
};

#define to_exynos9810_acpm_clk(_hw) \
	container_of(_hw, struct exynos9810_acpm_clk, hw)

static int exynos9810_acpm_clk_xfer(struct exynos9810_acpm_clk *aclk,
				    u32 rate)
{
	struct ipc_config config = {};
	u32 command[EXYNOS9810_ACPM_COMMAND_WORDS] = {
		aclk->id,
		rate,
		EXYNOS9810_ACPM_FREQ_SET,
		0,
	};
	int ret;

	config.cmd = command;
	config.response = true;

	ret = acpm_ipc_send_data_sync(aclk->channel, &config);
	if (ret)
		return ret;

	return 0;
}

static unsigned long exynos9810_acpm_clk_recalc_rate(struct clk_hw *hw,
						     unsigned long parent_rate)
{
	struct exynos9810_acpm_clk *aclk = to_exynos9810_acpm_clk(hw);

	return aclk->rate;
}

static int exynos9810_acpm_clk_determine_rate(struct clk_hw *hw,
					      struct clk_rate_request *req)
{
	if (req->rate % 1000)
		return -EINVAL;

	if (req->rate / 1000 > U32_MAX)
		return -ERANGE;

	return 0;
}

static int exynos9810_acpm_clk_set_rate(struct clk_hw *hw,
					unsigned long rate,
					unsigned long parent_rate)
{
	struct exynos9810_acpm_clk *aclk = to_exynos9810_acpm_clk(hw);
	u32 rate_khz = rate / 1000;
	int ret;

	if (rate == aclk->rate)
		return 0;

	ret = exynos9810_acpm_clk_xfer(aclk, rate_khz);
	if (ret)
		return ret;

	aclk->rate = rate;

	return 0;
}

static const struct clk_ops exynos9810_acpm_clk_ops = {
	.recalc_rate = exynos9810_acpm_clk_recalc_rate,
	.determine_rate = exynos9810_acpm_clk_determine_rate,
	.set_rate = exynos9810_acpm_clk_set_rate,
};

static const char *const exynos9810_acpm_clk_names[] = {
	[CLK_ACPM_DVFS_MIF] = "acpm_dvfs_mif",
	[CLK_ACPM_DVFS_INT] = "acpm_dvfs_int",
	[CLK_ACPM_DVFS_CPUCL0] = "acpm_dvfs_cpucl0",
	[CLK_ACPM_DVFS_CPUCL1] = "acpm_dvfs_cpucl1",
	[CLK_ACPM_DVFS_G3D] = "acpm_dvfs_g3d",
	[CLK_ACPM_DVFS_INTCAM] = "acpm_dvfs_intcam",
	[CLK_ACPM_DVFS_FSYS0] = "acpm_dvfs_fsys0",
	[CLK_ACPM_DVFS_CAM] = "acpm_dvfs_cam",
	[CLK_ACPM_DVFS_DISP] = "acpm_dvfs_disp",
	[CLK_ACPM_DVFS_AUD] = "acpm_dvfs_aud",
	[CLK_ACPM_DVFS_IVA] = "acpm_dvfs_iva",
	[CLK_ACPM_DVFS_SCORE] = "acpm_dvfs_score",
	[CLK_ACPM_DVFS_CP] = "acpm_dvfs_cp",
};

static const unsigned long
exynos9810_acpm_clk_initial_rates[ARRAY_SIZE(exynos9810_acpm_clk_names)] = {
	[CLK_ACPM_DVFS_CPUCL0] = 455000000,
	[CLK_ACPM_DVFS_G3D] = 260000000,
};

static int exynos9810_acpm_clk_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct exynos9810_acpm_clk *aclks;
	struct device *dev = &pdev->dev;
	unsigned int channel, size;
	unsigned int i;
	int ret;

	ret = acpm_ipc_request_channel(dev->of_node, NULL, &channel, &size);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to request ACPM channel\n");

	if (size != EXYNOS9810_ACPM_COMMAND_WORDS * sizeof(u32))
		return dev_err_probe(dev, -EINVAL,
				     "unsupported ACPM command size %u\n",
				     size);

	clk_data = devm_kzalloc(dev,
				struct_size(clk_data, hws,
					    ARRAY_SIZE(exynos9810_acpm_clk_names)),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	aclks = devm_kcalloc(dev, ARRAY_SIZE(exynos9810_acpm_clk_names),
			     sizeof(*aclks), GFP_KERNEL);
	if (!aclks)
		return -ENOMEM;

	clk_data->num = ARRAY_SIZE(exynos9810_acpm_clk_names);

	for (i = 0; i < clk_data->num; i++) {
		struct clk_init_data init = {
			.name = exynos9810_acpm_clk_names[i],
			.ops = &exynos9810_acpm_clk_ops,
		};
		struct exynos9810_acpm_clk *aclk = &aclks[i];

		aclk->channel = channel;
		aclk->id = i;
		aclk->rate = exynos9810_acpm_clk_initial_rates[i];
		aclk->hw.init = &init;

		ret = devm_clk_hw_register(dev, &aclk->hw);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to register clock %u\n", i);

		clk_data->hws[i] = &aclk->hw;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					  clk_data);
}

static const struct of_device_id exynos9810_acpm_clk_of_match[] = {
	{ .compatible = "samsung,exynos9810-acpm-dvfs" },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos9810_acpm_clk_of_match);

static struct platform_driver exynos9810_acpm_clk_driver = {
	.probe = exynos9810_acpm_clk_probe,
	.driver = {
		.name = "exynos9810-acpm-clk",
		.of_match_table = exynos9810_acpm_clk_of_match,
	},
};
module_platform_driver(exynos9810_acpm_clk_driver);

MODULE_AUTHOR("Mathias Gluszczynski <admin@krazey.de>");
MODULE_DESCRIPTION("Samsung Exynos9810 ACPM DVFS clock driver");
MODULE_LICENSE("GPL");
