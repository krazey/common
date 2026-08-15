// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-exynos.c - Samsung Exynos DWC3 Specific Glue layer
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#define DWC3_EXYNOS_MAX_CLOCKS	4

struct dwc3_exynos_driverdata {
	const char		*clk_names[DWC3_EXYNOS_MAX_CLOCKS];
	int			num_clks;
	int			suspend_clk_idx;
	bool			regulators_optional;
};

struct dwc3_exynos {
	struct device		*dev;
#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	struct delayed_work	diagnostics_work;
#endif

	const char		**clk_names;
	struct clk		*clks[DWC3_EXYNOS_MAX_CLOCKS];
	int			num_clks;
	int			suspend_clk_idx;

	struct regulator	*vdd33;
	struct regulator	*vdd10;
};

#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
static int dwc3_exynos_diagnostics_child(struct device *child, void *data)
{
	struct device *parent = data;
	const char *driver;
	bool bound;

	device_lock(child);
	bound = device_is_bound(child);
	driver = child->driver ? child->driver->name : "none";
	dev_info(parent, "E981D: USB child=%s bound=%u driver=%s\n",
		 dev_name(child), bound, driver);
	device_unlock(child);

	return 0;
}

static void dwc3_exynos_diagnostics_work(struct work_struct *work)
{
	struct dwc3_exynos *exynos =
		container_of(to_delayed_work(work), struct dwc3_exynos,
			     diagnostics_work);

	dev_info(exynos->dev, "E981D: USB wrapper clocks=%lu/%lu/%lu\n",
		 clk_get_rate(exynos->clks[0]), clk_get_rate(exynos->clks[1]),
		 clk_get_rate(exynos->clks[2]));
	dev_info(exynos->dev, "E981D: USB wrapper enabled=%d/%d/%d\n",
		 __clk_is_enabled(exynos->clks[0]),
		 __clk_is_enabled(exynos->clks[1]),
		 __clk_is_enabled(exynos->clks[2]));
	device_for_each_child(exynos->dev, exynos->dev,
			      dwc3_exynos_diagnostics_child);
}
#endif

static int dwc3_exynos_get_regulator(struct dwc3_exynos *exynos,
				     struct regulator **regulator,
				     const char *supply, bool optional)
{
	struct device *dev = exynos->dev;
	int ret;

	if (optional)
		*regulator = devm_regulator_get_optional(dev, supply);
	else
		*regulator = devm_regulator_get(dev, supply);

	if (IS_ERR(*regulator)) {
		ret = PTR_ERR(*regulator);
		if (optional && ret == -ENODEV) {
			*regulator = NULL;
			return 0;
		}

		return dev_err_probe(dev, ret, "failed to get %s supply\n",
				     supply);
	}

	ret = regulator_enable(*regulator);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable %s supply\n",
				     supply);

	return 0;
}

static int dwc3_exynos_probe(struct platform_device *pdev)
{
	struct dwc3_exynos	*exynos;
	struct device		*dev = &pdev->dev;
	struct device_node	*node = dev->of_node;
	const struct dwc3_exynos_driverdata *driver_data;
	int			i, ret;

	exynos = devm_kzalloc(dev, sizeof(*exynos), GFP_KERNEL);
	if (!exynos)
		return -ENOMEM;
#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	INIT_DELAYED_WORK(&exynos->diagnostics_work,
			  dwc3_exynos_diagnostics_work);
#endif

	driver_data = of_device_get_match_data(dev);
	exynos->dev = dev;
	exynos->num_clks = driver_data->num_clks;
	exynos->clk_names = (const char **)driver_data->clk_names;
	exynos->suspend_clk_idx = driver_data->suspend_clk_idx;

	platform_set_drvdata(pdev, exynos);

	for (i = 0; i < exynos->num_clks; i++) {
		exynos->clks[i] = devm_clk_get(dev, exynos->clk_names[i]);
		if (IS_ERR(exynos->clks[i])) {
			dev_err(dev, "failed to get clock: %s\n",
				exynos->clk_names[i]);
			return PTR_ERR(exynos->clks[i]);
		}
	}

	for (i = 0; i < exynos->num_clks; i++) {
		ret = clk_prepare_enable(exynos->clks[i]);
		if (ret) {
			while (i-- > 0)
				clk_disable_unprepare(exynos->clks[i]);
			return ret;
		}
	}

	if (exynos->suspend_clk_idx >= 0)
		clk_prepare_enable(exynos->clks[exynos->suspend_clk_idx]);

	ret = dwc3_exynos_get_regulator(exynos, &exynos->vdd33, "vdd33",
					driver_data->regulators_optional);
	if (ret)
		goto vdd33_err;

	ret = dwc3_exynos_get_regulator(exynos, &exynos->vdd10, "vdd10",
					driver_data->regulators_optional);
	if (ret)
		goto vdd10_err;

	if (node) {
		ret = of_platform_populate(node, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "failed to add dwc3 core\n");
			goto populate_err;
		}
	} else {
		dev_err(dev, "no device node, failed to add dwc3 core\n");
		ret = -ENODEV;
		goto populate_err;
	}

#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	if (of_device_is_compatible(node, "samsung,exynos9810-dwusb3"))
		schedule_delayed_work(&exynos->diagnostics_work, 10 * HZ);
#endif

	return 0;

populate_err:
	if (exynos->vdd10)
		regulator_disable(exynos->vdd10);
vdd10_err:
	if (exynos->vdd33)
		regulator_disable(exynos->vdd33);
vdd33_err:
	for (i = exynos->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(exynos->clks[i]);

	if (exynos->suspend_clk_idx >= 0)
		clk_disable_unprepare(exynos->clks[exynos->suspend_clk_idx]);

	return ret;
}

static void dwc3_exynos_remove(struct platform_device *pdev)
{
	struct dwc3_exynos	*exynos = platform_get_drvdata(pdev);
	int i;

#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	cancel_delayed_work_sync(&exynos->diagnostics_work);
#endif
	of_platform_depopulate(&pdev->dev);

	for (i = exynos->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(exynos->clks[i]);

	if (exynos->suspend_clk_idx >= 0)
		clk_disable_unprepare(exynos->clks[exynos->suspend_clk_idx]);

	if (exynos->vdd33)
		regulator_disable(exynos->vdd33);
	if (exynos->vdd10)
		regulator_disable(exynos->vdd10);
}

static const struct dwc3_exynos_driverdata exynos2200_drvdata = {
	.clk_names = { "link_aclk" },
	.num_clks = 1,
	.suspend_clk_idx = -1,
};

static const struct dwc3_exynos_driverdata exynos5250_drvdata = {
	.clk_names = { "usbdrd30" },
	.num_clks = 1,
	.suspend_clk_idx = -1,
};

static const struct dwc3_exynos_driverdata exynos5433_drvdata = {
	.clk_names = { "aclk", "susp_clk", "pipe_pclk", "phyclk" },
	.num_clks = 4,
	.suspend_clk_idx = 1,
};

static const struct dwc3_exynos_driverdata exynos7_drvdata = {
	.clk_names = { "usbdrd30", "usbdrd30_susp_clk", "usbdrd30_axius_clk" },
	.num_clks = 3,
	.suspend_clk_idx = 1,
};

static const struct dwc3_exynos_driverdata exynos7870_drvdata = {
	.clk_names = { "bus_early", "ref", "ctrl" },
	.num_clks = 3,
	.suspend_clk_idx = -1,
};

static const struct dwc3_exynos_driverdata exynos850_drvdata = {
	.clk_names = { "bus_early", "ref" },
	.num_clks = 2,
	.suspend_clk_idx = -1,
};

static const struct dwc3_exynos_driverdata exynos9810_drvdata = {
	.clk_names = { "aclk", "sclk", "ctrl" },
	.num_clks = 3,
	.suspend_clk_idx = -1,
	.regulators_optional = true,
};

static const struct dwc3_exynos_driverdata gs101_drvdata = {
	.clk_names = { "bus_early", "susp_clk", "link_aclk", "link_pclk" },
	.num_clks = 4,
	.suspend_clk_idx = 1,
};

static const struct dwc3_exynos_driverdata exynosautov920_drvdata = {
	.clk_names = { "ref", "susp_clk"},
	.num_clks = 2,
	.suspend_clk_idx = 1,
};

static const struct of_device_id exynos_dwc3_match[] = {
	{
		.compatible = "samsung,exynos2200-dwusb3",
		.data = &exynos2200_drvdata,
	}, {
		.compatible = "samsung,exynos5250-dwusb3",
		.data = &exynos5250_drvdata,
	}, {
		.compatible = "samsung,exynos5433-dwusb3",
		.data = &exynos5433_drvdata,
	}, {
		.compatible = "samsung,exynos7-dwusb3",
		.data = &exynos7_drvdata,
	}, {
		.compatible = "samsung,exynos7870-dwusb3",
		.data = &exynos7870_drvdata,
	}, {
		.compatible = "samsung,exynos850-dwusb3",
		.data = &exynos850_drvdata,
	}, {
		.compatible = "samsung,exynos9810-dwusb3",
		.data = &exynos9810_drvdata,
	}, {
		.compatible = "samsung,exynosautov920-dwusb3",
		.data = &exynosautov920_drvdata,
	}, {
		.compatible = "google,gs101-dwusb3",
		.data = &gs101_drvdata,
	}, {
	}
};
MODULE_DEVICE_TABLE(of, exynos_dwc3_match);

static int dwc3_exynos_suspend(struct device *dev)
{
	struct dwc3_exynos *exynos = dev_get_drvdata(dev);
	int i;

	for (i = exynos->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(exynos->clks[i]);

	if (exynos->vdd33)
		regulator_disable(exynos->vdd33);
	if (exynos->vdd10)
		regulator_disable(exynos->vdd10);

	return 0;
}

static int dwc3_exynos_resume(struct device *dev)
{
	struct dwc3_exynos *exynos = dev_get_drvdata(dev);
	int i, ret;

	if (exynos->vdd33) {
		ret = regulator_enable(exynos->vdd33);
		if (ret) {
			dev_err(dev, "Failed to enable VDD33 supply\n");
			return ret;
		}
	}
	if (exynos->vdd10) {
		ret = regulator_enable(exynos->vdd10);
		if (ret) {
			dev_err(dev, "Failed to enable VDD10 supply\n");
			if (exynos->vdd33)
				regulator_disable(exynos->vdd33);
			return ret;
		}
	}

	for (i = 0; i < exynos->num_clks; i++) {
		ret = clk_prepare_enable(exynos->clks[i]);
		if (ret) {
			while (i-- > 0)
				clk_disable_unprepare(exynos->clks[i]);
			return ret;
		}
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(dwc3_exynos_dev_pm_ops,
				dwc3_exynos_suspend, dwc3_exynos_resume);

static struct platform_driver dwc3_exynos_driver = {
	.probe		= dwc3_exynos_probe,
	.remove		= dwc3_exynos_remove,
	.driver		= {
		.name	= "exynos-dwc3",
		.of_match_table = exynos_dwc3_match,
		.pm	= pm_sleep_ptr(&dwc3_exynos_dev_pm_ops),
	},
};

module_platform_driver(dwc3_exynos_driver);

MODULE_AUTHOR("Anton Tikhomirov <av.tikhomirov@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 Exynos Glue Layer");
