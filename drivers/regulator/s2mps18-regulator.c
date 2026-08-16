// SPDX-License-Identifier: GPL-2.0
/* Minimal Samsung S2MPS18 regulator support for Exynos9810. */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <soc/samsung/acpm_mfd.h>

#define S2MPS18_PMIC_ADDR	0x01
#define S2MPS18_L35CTRL		0x6c
#define S2MPS18_L43CTRL		0x74
#define S2MPS18_ENABLE_MASK	GENMASK(7, 6)
#define S2MPS18_ENABLE_ON	S2MPS18_ENABLE_MASK
#define S2MPS18_VSEL_MASK	GENMASK(5, 0)

struct device_node *acpm_mfd_node;

static int s2mps18_reg_enable(struct regulator_dev *rdev)
{
	return exynos_acpm_update_reg(S2MPS18_PMIC_ADDR,
				      rdev->desc->enable_reg,
				      S2MPS18_ENABLE_ON,
				      S2MPS18_ENABLE_MASK);
}

static int s2mps18_reg_disable(struct regulator_dev *rdev)
{
	return exynos_acpm_update_reg(S2MPS18_PMIC_ADDR,
				      rdev->desc->enable_reg, 0,
				      S2MPS18_ENABLE_MASK);
}

static int s2mps18_reg_is_enabled(struct regulator_dev *rdev)
{
	u8 val;
	int ret;

	ret = exynos_acpm_read_reg(S2MPS18_PMIC_ADDR,
				   rdev->desc->enable_reg, &val);
	if (ret)
		return ret;

	return !!(val & S2MPS18_ENABLE_MASK);
}

static int s2mps18_reg_get_voltage_sel(struct regulator_dev *rdev)
{
	u8 val;
	int ret;

	ret = exynos_acpm_read_reg(S2MPS18_PMIC_ADDR,
				   rdev->desc->vsel_reg, &val);
	if (ret)
		return ret;

	return val & S2MPS18_VSEL_MASK;
}

static int s2mps18_reg_set_voltage_sel(struct regulator_dev *rdev,
				       unsigned int sel)
{
	return exynos_acpm_update_reg(S2MPS18_PMIC_ADDR,
				      rdev->desc->vsel_reg, sel,
				      S2MPS18_VSEL_MASK);
}

static const struct regulator_ops s2mps18_reg_ops = {
	.enable = s2mps18_reg_enable,
	.disable = s2mps18_reg_disable,
	.is_enabled = s2mps18_reg_is_enabled,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = s2mps18_reg_get_voltage_sel,
	.set_voltage_sel = s2mps18_reg_set_voltage_sel,
};

static const struct regulator_desc s2mps18_regulators[] = {
	{
		.name = "LDO35",
		.of_match = "LDO35",
		.id = 35,
		.ops = &s2mps18_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.min_uV = 700000,
		.uV_step = 25000,
		.n_voltages = 64,
		.vsel_reg = S2MPS18_L35CTRL,
		.vsel_mask = S2MPS18_VSEL_MASK,
		.enable_reg = S2MPS18_L35CTRL,
		.enable_mask = S2MPS18_ENABLE_MASK,
	}, {
		.name = "LDO43",
		.of_match = "LDO43",
		.id = 43,
		.ops = &s2mps18_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.min_uV = 1800000,
		.uV_step = 25000,
		.n_voltages = 64,
		.vsel_reg = S2MPS18_L43CTRL,
		.vsel_mask = S2MPS18_VSEL_MASK,
		.enable_reg = S2MPS18_L43CTRL,
		.enable_mask = S2MPS18_ENABLE_MASK,
	},
};

static int s2mps18_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	u8 val;
	int i;
	int ret;

	acpm_mfd_node = pdev->dev.of_node;
	config.dev = &pdev->dev;

	ret = exynos_acpm_read_reg(S2MPS18_PMIC_ADDR,
				   S2MPS18_L35CTRL, &val);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to contact S2MPS18\n");

	dev_info(&pdev->dev, "S2MPS18 LDO35 initial value %#02x\n", val);

	for (i = 0; i < ARRAY_SIZE(s2mps18_regulators); i++) {
		config.of_node = of_get_child_by_name(pdev->dev.of_node,
						      s2mps18_regulators[i].of_match);
		if (!config.of_node)
			continue;

		rdev = devm_regulator_register(&pdev->dev,
					       &s2mps18_regulators[i],
					       &config);
		of_node_put(config.of_node);
		if (IS_ERR(rdev))
			return dev_err_probe(&pdev->dev, PTR_ERR(rdev),
					     "failed to register %s\n",
					     s2mps18_regulators[i].name);
	}

	return 0;
}

static const struct of_device_id s2mps18_regulator_of_match[] = {
	{ .compatible = "samsung,s2mps18-regulator" },
	{ }
};
MODULE_DEVICE_TABLE(of, s2mps18_regulator_of_match);

static struct platform_driver s2mps18_regulator_driver = {
	.probe = s2mps18_regulator_probe,
	.driver = {
		.name = "s2mps18-regulator",
		.of_match_table = s2mps18_regulator_of_match,
	},
};
module_platform_driver(s2mps18_regulator_driver);

MODULE_DESCRIPTION("Samsung S2MPS18 regulator driver");
MODULE_LICENSE("GPL");
