// SPDX-License-Identifier: GPL-2.0+
/*
 * Maxim MAX77705 PMIC core driver
 *
 * Copyright (C) 2025 Dzmitry Sankouski <dsankouski@gmail.com>
 **/
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77705-private.h>
#include <linux/mfd/max77693-common.h>
#include <linux/pm.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of.h>

#define MAX77705_REVISION_RETRIES	5

static const struct resource max77705_muic_resources[] = {
	DEFINE_RES_IRQ_NAMED(MAX77705_IRQ_USBC, "usbc"),
};

static struct mfd_cell max77705_devs[] = {
	MFD_CELL_OF("max77705-muic", max77705_muic_resources, NULL, 0, 0,
		    "maxim,max77705-muic"),
	MFD_CELL_OF("max77705-rgb", NULL, NULL, 0, 0, "maxim,max77705-rgb"),
	MFD_CELL_OF("max77705-haptic", NULL, NULL, 0, 0, "maxim,max77705-haptic"),
};

static const struct regmap_range max77705_readable_ranges[] = {
	regmap_reg_range(MAX77705_PMIC_REG_PMICID1,		MAX77705_PMIC_REG_BSTOUT_MASK),
	regmap_reg_range(MAX77705_PMIC_REG_INTSRC,		MAX77705_PMIC_REG_RESERVED_29),
	regmap_reg_range(MAX77705_PMIC_REG_BOOSTCONTROL1,	MAX77705_PMIC_REG_BOOSTCONTROL1),
	regmap_reg_range(MAX77705_PMIC_REG_MCONFIG,		MAX77705_PMIC_REG_MCONFIG2),
	regmap_reg_range(MAX77705_PMIC_REG_FORCE_EN_MASK,	MAX77705_PMIC_REG_FORCE_EN_MASK),
	regmap_reg_range(MAX77705_PMIC_REG_BOOSTCONTROL1,	MAX77705_PMIC_REG_BOOSTCONTROL1),
	regmap_reg_range(MAX77705_PMIC_REG_BOOSTCONTROL2,	MAX77705_PMIC_REG_BOOSTCONTROL2),
	regmap_reg_range(MAX77705_PMIC_REG_SW_RESET,		MAX77705_PMIC_REG_USBC_RESET),
};

static const struct regmap_range max77705_writable_ranges[] = {
	regmap_reg_range(MAX77705_PMIC_REG_MAINCTRL1,		MAX77705_PMIC_REG_BSTOUT_MASK),
	regmap_reg_range(MAX77705_PMIC_REG_INTSRC,		MAX77705_PMIC_REG_RESERVED_29),
	regmap_reg_range(MAX77705_PMIC_REG_BOOSTCONTROL1,	MAX77705_PMIC_REG_BOOSTCONTROL1),
	regmap_reg_range(MAX77705_PMIC_REG_MCONFIG,		MAX77705_PMIC_REG_MCONFIG2),
	regmap_reg_range(MAX77705_PMIC_REG_FORCE_EN_MASK,	MAX77705_PMIC_REG_FORCE_EN_MASK),
	regmap_reg_range(MAX77705_PMIC_REG_BOOSTCONTROL1,	MAX77705_PMIC_REG_BOOSTCONTROL1),
	regmap_reg_range(MAX77705_PMIC_REG_BOOSTCONTROL2,	MAX77705_PMIC_REG_BOOSTCONTROL2),
	regmap_reg_range(MAX77705_PMIC_REG_SW_RESET,		MAX77705_PMIC_REG_USBC_RESET),
};

static const struct regmap_access_table max77705_readable_table = {
	.yes_ranges = max77705_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max77705_readable_ranges),
};

static const struct regmap_access_table max77705_writable_table = {
	.yes_ranges = max77705_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max77705_writable_ranges),
};

static const struct regmap_config max77705_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &max77705_readable_table,
	.wr_table = &max77705_writable_table,
	.max_register = MAX77705_PMIC_REG_USBC_RESET,
};

static const struct regmap_irq max77705_irqs[] = {
	[MAX77705_IRQ_CHG] = { .mask = MAX77705_SRC_IRQ_CHG, },
	[MAX77705_IRQ_TOP] = { .mask = MAX77705_SRC_IRQ_TOP, },
	[MAX77705_IRQ_FG] = { .mask = MAX77705_SRC_IRQ_FG, },
	[MAX77705_IRQ_USBC] = { .mask = MAX77705_SRC_IRQ_USBC, },
};

static const struct regmap_irq_chip max77705_irq_chip = {
	.name		= "max77705",
	.status_base	= MAX77705_PMIC_REG_INTSRC,
	.ack_base	= MAX77705_PMIC_REG_INTSRC,
	.mask_base	= MAX77705_PMIC_REG_INTSRC_MASK,
	.num_regs	= 1,
	.irqs		= max77705_irqs,
	.num_irqs	= ARRAY_SIZE(max77705_irqs),
};

static int max77705_read_revision(struct regmap *regmap,
				  unsigned int *revision)
{
	int attempt;
	int ret;

	for (attempt = 0; attempt < MAX77705_REVISION_RETRIES; attempt++) {
		ret = regmap_read(regmap, MAX77705_PMIC_REG_PMICREV,
				  revision);
		if (!ret)
			return 0;
		if (attempt + 1 < MAX77705_REVISION_RETRIES)
			usleep_range(10000, 20000);
	}

	return ret;
}

static int max77705_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct max77693_dev *max77705;
	struct regmap_irq_chip_data *irq_data;
	struct irq_domain *domain;
	enum max77705_hw_rev pmic_rev;
	unsigned int pmic_rev_value;
	int ret;

	max77705 = devm_kzalloc(dev, sizeof(*max77705), GFP_KERNEL);
	if (!max77705)
		return -ENOMEM;

	max77705->i2c = i2c;
	max77705->type = TYPE_MAX77705;
	i2c_set_clientdata(i2c, max77705);

	max77705->regmap = devm_regmap_init_i2c(i2c, &max77705_regmap_config);
	if (IS_ERR(max77705->regmap))
		return PTR_ERR(max77705->regmap);

	ret = max77705_read_revision(max77705->regmap, &pmic_rev_value);
	if (ret) {
		if (ret == -ETIMEDOUT || ret == -EAGAIN)
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "PMIC revision read failed: %d\n",
					     ret);
		return dev_err_probe(dev, ret,
				     "Failed to read PMIC revision\n");
	}

	pmic_rev = pmic_rev_value & MAX77705_REVISION_MASK;
	if (pmic_rev < MAX77705_PASS1 || pmic_rev > MAX77705_PASS3)
		return dev_err_probe(dev, -ENODEV, "Rev.0x%x is not tested\n", pmic_rev);

	dev_info(dev, "E981D: MAX77705 revision=%#x version=%#x irq=%d\n",
		 pmic_rev, pmic_rev_value >> 3, i2c->irq);

	/* Active Discharge Enable */
	ret = regmap_update_bits(max77705->regmap,
				 MAX77705_PMIC_REG_MAINCTRL1, 1, 1);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to enable active discharge\n");

	max77705->i2c_muic = devm_i2c_new_dummy_device(dev, i2c->adapter,
						       MAX77705_I2C_ADDR_MUIC);
	if (IS_ERR(max77705->i2c_muic))
		return dev_err_probe(dev, PTR_ERR(max77705->i2c_muic),
				     "Failed to register MUIC I2C client\n");

	ret = devm_regmap_add_irq_chip(dev, max77705->regmap,
					i2c->irq,
					IRQF_ONESHOT, 0,
					&max77705_irq_chip,
					&irq_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add IRQ chip\n");

	domain = regmap_irq_get_domain(irq_data);

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
				   max77705_devs, ARRAY_SIZE(max77705_devs),
				   NULL, 0, domain);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register child devices\n");

	ret = devm_device_init_wakeup(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init wakeup\n");

	dev_info(dev, "E981D: MAX77705 MFD children registered\n");

	return 0;
}

static int max77705_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);

	disable_irq(i2c->irq);

	if (device_may_wakeup(dev))
		enable_irq_wake(i2c->irq);

	return 0;
}

static int max77705_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(i2c->irq);

	enable_irq(i2c->irq);

	return 0;
}
static DEFINE_SIMPLE_DEV_PM_OPS(max77705_pm_ops, max77705_suspend, max77705_resume);

static const struct of_device_id max77705_i2c_of_match[] = {
	{ .compatible = "maxim,max77705" },
	{ }
};
MODULE_DEVICE_TABLE(of, max77705_i2c_of_match);

static struct i2c_driver max77705_i2c_driver = {
	.driver = {
		.name			= "max77705",
		.of_match_table		= max77705_i2c_of_match,
		.pm			= pm_sleep_ptr(&max77705_pm_ops),
	},
	.probe = max77705_i2c_probe
};
module_i2c_driver(max77705_i2c_driver);

MODULE_DESCRIPTION("Maxim MAX77705 PMIC core driver");
MODULE_AUTHOR("Dzmitry Sankouski <dsankouski@gmail.com>");
MODULE_LICENSE("GPL");
