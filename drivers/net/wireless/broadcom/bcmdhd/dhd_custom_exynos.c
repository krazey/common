// SPDX-License-Identifier: GPL-2.0
/*
 * Platform data for Samsung Exynos WLAN devices
 *
 * Copyright (C) 2021, Broadcom.
 * Copyright (C) 2026 Mathias Gluszczynski
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/printk.h>

#include "dhd_linux.h"

static int wlan_host_wake_gpio = -EINVAL;
static int wlan_host_wake_irq;

static int dhd_wlan_power(int on)
{
	/* WLAN_REG_ON is held by the PCIe host's vpcie3v3 supply. */
	pr_debug("bcmdhd: WLAN power request %d\n", on);

	return 0;
}

static int dhd_wlan_reset(int on)
{
	return 0;
}

static int dhd_wlan_set_carddetect(int present)
{
	/* The endpoint is enumerated with the built-in PCIe host. */
	pr_debug("bcmdhd: card-detect request %d\n", present);

	return 0;
}

static int dhd_wlan_init_gpio(void)
{
	struct device_node *np;
	int gpio;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "samsung,brcm-wlan");
	if (!np)
		return -ENODEV;

	gpio = of_get_named_gpio(np, "host-wake-gpios", 0);
	of_node_put(np);
	if (!gpio_is_valid(gpio))
		return gpio < 0 ? gpio : -EINVAL;

	ret = gpio_request_one(gpio, GPIOF_IN, "WLAN_HOST_WAKE");
	if (ret)
		return ret;

	wlan_host_wake_irq = gpio_to_irq(gpio);
	if (wlan_host_wake_irq < 0) {
		ret = wlan_host_wake_irq;
		gpio_free(gpio);
		return ret;
	}

	wlan_host_wake_gpio = gpio;

	return 0;
}

#if defined(CONFIG_BCMDHD_OOB_HOST_WAKE) && \
	defined(CONFIG_BCMDHD_GET_OOB_STATE)
int dhd_get_wlan_oob_gpio(void)
{
	if (!gpio_is_valid(wlan_host_wake_gpio))
		return -ENODEV;

	return gpio_get_value(wlan_host_wake_gpio);
}
EXPORT_SYMBOL(dhd_get_wlan_oob_gpio);

int dhd_get_wlan_oob_gpio_number(void)
{
	return gpio_is_valid(wlan_host_wake_gpio) ?
		wlan_host_wake_gpio : -ENODEV;
}
EXPORT_SYMBOL(dhd_get_wlan_oob_gpio_number);
#endif

struct resource dhd_wlan_resources = {
	.name = "bcmdhd_wlan_irq",
	.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE |
		 IORESOURCE_IRQ_HIGHEDGE,
};
EXPORT_SYMBOL(dhd_wlan_resources);

struct wifi_platform_data dhd_wlan_control = {
	.set_power = dhd_wlan_power,
	.set_reset = dhd_wlan_reset,
	.set_carddetect = dhd_wlan_set_carddetect,
};
EXPORT_SYMBOL(dhd_wlan_control);

int dhd_wlan_init(void)
{
	int ret;

	ret = dhd_wlan_init_gpio();
	if (ret) {
		pr_err("bcmdhd: failed to initialize host wake: %d\n", ret);
		return ret;
	}

	dhd_wlan_resources.start = wlan_host_wake_irq;
	dhd_wlan_resources.end = wlan_host_wake_irq;
	pr_info("bcmdhd: host wake GPIO %d IRQ %d\n",
		wlan_host_wake_gpio, wlan_host_wake_irq);

	return 0;
}

int dhd_wlan_deinit(void)
{
	if (gpio_is_valid(wlan_host_wake_gpio)) {
		gpio_free(wlan_host_wake_gpio);
		wlan_host_wake_gpio = -EINVAL;
	}

	return 0;
}

late_initcall(dhd_wlan_init);
