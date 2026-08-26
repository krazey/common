// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim MAX77705 USB data path multiplexer
 *
 * Copyright (C) 2026 Mathias Gluszczynski <admin@krazey.de>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/extcon-provider.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mux/driver.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define MAX77705_UIC_INT		0x02
#define MAX77705_UIC_INT_M		0x0e
#define MAX77705_UIC_INT_APCMDRES	BIT(7)
#define MAX77705_UIC_INT_SYSMSG		BIT(6)
#define MAX77705_UIC_INT_RESERVED	BIT(2)
#define MAX77705_UIC_INT_DRIVER_MASK	(MAX77705_UIC_INT_APCMDRES | \
					 MAX77705_UIC_INT_SYSMSG | \
					 MAX77705_UIC_INT_RESERVED)
#define MAX77705_USBC_STATUS1		0x06
#define MAX77705_BC_STATUS		0x08
#define MAX77705_OPCODE_WRITE		0x21
#define MAX77705_OPCODE_WRITE_END	0x41
#define MAX77705_OPCODE_READ		0x51
#define MAX77705_OPCODE_CTRL1_WRITE	0x06

#define MAX77705_CTRL1_COM_OPEN		0x3f
#define MAX77705_CTRL1_COM_USB		0x09

#define MAX77705_UIADC_GND		0x00
#define MAX77705_UIADC_MASK		GENMASK(2, 0)
#define MAX77705_UIADC_JIG_USB_OFF	0x03
#define MAX77705_UIADC_JIG_USB_ON	0x04
#define MAX77705_UIADC_OPEN		0x07
#define MAX77705_BC_CHGTYP_MASK		GENMASK(1, 0)
#define MAX77705_BC_CHGTYP_USB		0x01
#define MAX77705_BC_CHGTYP_CDP		0x02
#define MAX77705_BC_CHGTYP_DCP		0x03
#define MAX77705_BC_PRCHGTYP_MASK	GENMASK(5, 3)
#define MAX77705_BC_PRCHGTYP_SHIFT	3
#define MAX77705_BC_DCD_TIMEOUT		BIT(2)
#define MAX77705_BC_VBUS_PRESENT	BIT(7)

#define MAX77705_MUIC_OPEN		0
#define MAX77705_MUIC_USB		1
#define MAX77705_MUIC_STATES		2
#define MAX77705_OPCODE_TIMEOUT_MS	3000

enum max77705_muic_cable {
	MAX77705_CABLE_NONE,
	MAX77705_CABLE_USB,
	MAX77705_CABLE_CDP,
	MAX77705_CABLE_DCP,
	MAX77705_CABLE_SLOW,
	MAX77705_CABLE_HOST,
};

struct max77705_muic {
	struct i2c_client *i2c;
	struct extcon_dev *edev;
	enum max77705_muic_cable cable;
	/* Serializes commands sent to the controller firmware. */
	struct mutex lock;
};

static const unsigned int max77705_muic_extcon_cables[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_SLOW,
	EXTCON_NONE,
};

static enum max77705_muic_cable
max77705_muic_decode_cable(int usbc_status, int bc_status)
{
	int chg_type = bc_status & MAX77705_BC_CHGTYP_MASK;
	int special = (bc_status & MAX77705_BC_PRCHGTYP_MASK) >>
		MAX77705_BC_PRCHGTYP_SHIFT;
	int uiadc = usbc_status & MAX77705_UIADC_MASK;

	if (uiadc == MAX77705_UIADC_GND)
		return MAX77705_CABLE_HOST;
	if (!(bc_status & MAX77705_BC_VBUS_PRESENT))
		return MAX77705_CABLE_NONE;
	if (uiadc == MAX77705_UIADC_JIG_USB_OFF ||
	    uiadc == MAX77705_UIADC_JIG_USB_ON)
		return MAX77705_CABLE_USB;
	if ((bc_status & MAX77705_BC_DCD_TIMEOUT) &&
	    chg_type == MAX77705_BC_CHGTYP_USB)
		return MAX77705_CABLE_SLOW;
	if (special && (chg_type == MAX77705_BC_CHGTYP_USB ||
			chg_type == MAX77705_BC_CHGTYP_CDP))
		return MAX77705_CABLE_DCP;

	switch (chg_type) {
	case MAX77705_BC_CHGTYP_USB:
		return MAX77705_CABLE_USB;
	case MAX77705_BC_CHGTYP_CDP:
		return MAX77705_CABLE_CDP;
	case MAX77705_BC_CHGTYP_DCP:
		return MAX77705_CABLE_DCP;
	default:
		/* Keep VBUS usable while charger detection is inconclusive. */
		return MAX77705_CABLE_SLOW;
	}
}

static bool max77705_muic_extcon_state(enum max77705_muic_cable cable, unsigned int id)
{
	switch (id) {
	case EXTCON_USB:
		return cable == MAX77705_CABLE_USB ||
		       cable == MAX77705_CABLE_CDP ||
		       cable == MAX77705_CABLE_SLOW;
	case EXTCON_USB_HOST:
		return cable == MAX77705_CABLE_HOST;
	case EXTCON_CHG_USB_SDP:
		return cable == MAX77705_CABLE_USB;
	case EXTCON_CHG_USB_CDP:
		return cable == MAX77705_CABLE_CDP;
	case EXTCON_CHG_USB_DCP:
		return cable == MAX77705_CABLE_DCP;
	case EXTCON_CHG_USB_SLOW:
		return cable == MAX77705_CABLE_SLOW;
	default:
		return false;
	}
}

static int max77705_muic_publish_cable(struct max77705_muic *muic, int usbc_status, int bc_status)
{
	bool changed[ARRAY_SIZE(max77705_muic_extcon_cables) - 1];
	enum max77705_muic_cable cable;
	unsigned int id;
	bool state;
	int old_state;
	int ret;
	int i;

	cable = max77705_muic_decode_cable(usbc_status, bc_status);
	for (i = 0; i < ARRAY_SIZE(changed); i++) {
		id = max77705_muic_extcon_cables[i];
		state = max77705_muic_extcon_state(cable, id);
		old_state = extcon_get_state(muic->edev, id);
		if (old_state < 0)
			return old_state;
		changed[i] = old_state != state;

		ret = extcon_set_state(muic->edev, id, state);
		if (ret)
			return ret;
	}

	if (muic->cable != cable)
		dev_info(&muic->i2c->dev,
			 "MAX77705 cable=%u usbc=%#x bc=%#x\n",
			 cable, usbc_status, bc_status);
	muic->cable = cable;

	/* Publish only after every connector reflects the new cable. */
	for (i = 0; i < ARRAY_SIZE(changed); i++) {
		if (!changed[i])
			continue;

		ret = extcon_sync(muic->edev,
				  max77705_muic_extcon_cables[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int max77705_muic_refresh_locked(struct max77705_muic *muic)
{
	int bc_status;
	int usbc_status;

	usbc_status = i2c_smbus_read_byte_data(muic->i2c,
					       MAX77705_USBC_STATUS1);
	if (usbc_status < 0)
		return usbc_status;

	bc_status = i2c_smbus_read_byte_data(muic->i2c,
					     MAX77705_BC_STATUS);
	if (bc_status < 0)
		return bc_status;

	return max77705_muic_publish_cable(muic, usbc_status, bc_status);
}

static int max77705_muic_wait_response(struct max77705_muic *muic,
				       int *last_status)
{
	int response;
	int status;
	unsigned long deadline;

	deadline = jiffies + msecs_to_jiffies(MAX77705_OPCODE_TIMEOUT_MS);
	do {
		status = i2c_smbus_read_byte_data(muic->i2c,
						  MAX77705_UIC_INT);
		if (status < 0)
			return status;
		if (status & MAX77705_UIC_INT_APCMDRES)
			break;
		usleep_range(1000, 2000);
	} while (time_before(jiffies, deadline));
	if (!(status & MAX77705_UIC_INT_APCMDRES))
		return -ETIMEDOUT;

	response = i2c_smbus_read_byte_data(muic->i2c,
					    MAX77705_OPCODE_READ);
	if (response < 0)
		return response;
	if (response != MAX77705_OPCODE_CTRL1_WRITE)
		return -EPROTO;

	*last_status = status;

	return 0;
}

static int max77705_muic_write_path(struct max77705_muic *muic, u8 path,
				    int *last_status)
{
	u8 command[] = {
		MAX77705_OPCODE_WRITE,
		MAX77705_OPCODE_CTRL1_WRITE,
		path,
	};
	int ret;

	/* Discard a response left by firmware before starting a command. */
	ret = i2c_smbus_read_byte_data(muic->i2c, MAX77705_UIC_INT);
	if (ret < 0)
		return ret;

	ret = i2c_master_send(muic->i2c, command, sizeof(command));
	if (ret < 0)
		return ret;
	if (ret != (int)sizeof(command))
		return -EIO;

	ret = i2c_smbus_write_byte_data(muic->i2c,
					MAX77705_OPCODE_WRITE_END, 0);
	if (ret < 0)
		return ret;

	return max77705_muic_wait_response(muic, last_status);
}

static int max77705_muic_switch_path(struct max77705_muic *muic, u8 path,
				     int *status)
{
	int ret;

	mutex_lock(&muic->lock);
	ret = max77705_muic_write_path(muic, path, status);
	mutex_unlock(&muic->lock);

	return ret;
}

static bool max77705_muic_is_boot_usb(int usbc_status, int bc_status)
{
	int chg_type = bc_status & MAX77705_BC_CHGTYP_MASK;
	int uiadc = usbc_status & MAX77705_UIADC_MASK;

	if (!(bc_status & MAX77705_BC_VBUS_PRESENT))
		return false;
	if (uiadc == MAX77705_UIADC_JIG_USB_OFF ||
	    uiadc == MAX77705_UIADC_JIG_USB_ON)
		return true;
	if (uiadc != MAX77705_UIADC_OPEN)
		return false;

	return chg_type == MAX77705_BC_CHGTYP_USB ||
	       chg_type == MAX77705_BC_CHGTYP_CDP ||
	       (bc_status & MAX77705_BC_DCD_TIMEOUT);
}

static void max77705_muic_detect_boot_usb(struct max77705_muic *muic)
{
	struct device *dev = &muic->i2c->dev;
	int bc_status;
	int status;
	int usbc_status;
	int ret;

	usbc_status = i2c_smbus_read_byte_data(muic->i2c,
					       MAX77705_USBC_STATUS1);
	if (usbc_status < 0) {
		dev_warn(dev, "failed to read initial USBC status: %d\n",
			 usbc_status);
		return;
	}

	bc_status = i2c_smbus_read_byte_data(muic->i2c,
					     MAX77705_BC_STATUS);
	if (bc_status < 0) {
		dev_warn(dev, "failed to read initial BC status: %d\n",
			 bc_status);
		return;
	}

	dev_info(dev, "E981D: MAX77705 initial usbc=%#x bc=%#x\n",
		 usbc_status, bc_status);
	ret = max77705_muic_publish_cable(muic, usbc_status, bc_status);
	if (ret)
		dev_warn(dev, "failed to publish initial cable: %d\n", ret);

	if (!max77705_muic_is_boot_usb(usbc_status, bc_status))
		return;

	ret = max77705_muic_switch_path(muic,
					MAX77705_CTRL1_COM_USB, &status);
	if (ret) {
		dev_warn(dev, "failed to route initial USB connection: %d\n",
			 ret);
		return;
	}

	dev_info(dev, "E981D: MAX77705 initial path=usb status=%#x\n",
		 status);
}

static irqreturn_t max77705_muic_irq(int irq, void *data)
{
	struct max77705_muic *muic = data;
	int interrupt;
	int ret;

	mutex_lock(&muic->lock);
	interrupt = i2c_smbus_read_byte_data(muic->i2c,
					     MAX77705_UIC_INT);
	ret = interrupt < 0 ? interrupt :
		max77705_muic_refresh_locked(muic);
	mutex_unlock(&muic->lock);

	if (ret)
		dev_err(&muic->i2c->dev, "failed to update cable: %d\n", ret);

	return IRQ_HANDLED;
}

static int max77705_muic_set(struct mux_control *mux, int state)
{
	struct max77705_muic *muic = mux_chip_priv(mux->chip);
	u8 path;
	int status;
	int ret;

	switch (state) {
	case MAX77705_MUIC_OPEN:
		path = MAX77705_CTRL1_COM_OPEN;
		break;
	case MAX77705_MUIC_USB:
		path = MAX77705_CTRL1_COM_USB;
		break;
	default:
		return -EINVAL;
	}

	ret = max77705_muic_switch_path(muic, path, &status);
	if (ret)
		return dev_err_probe(&muic->i2c->dev, ret,
				     "failed to switch USB data path\n");

	dev_info(&muic->i2c->dev, "E981D: MAX77705 path=%s status=%#x\n",
		 state == MAX77705_MUIC_USB ? "usb" : "open", status);

	return 0;
}

static const struct mux_control_ops max77705_muic_ops = {
	.set = max77705_muic_set,
};

static int max77705_muic_probe(struct platform_device *pdev)
{
	struct max77693_dev *max77705 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct max77705_muic *muic;
	struct mux_chip *mux_chip;
	s32 idle_state;
	int irq;
	int ret;

	if (!max77705 || !max77705->i2c_muic)
		return -ENODEV;
	if (!i2c_check_functionality(max77705->i2c_muic->adapter,
				     I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	mux_chip = devm_mux_chip_alloc(dev, 1, sizeof(*muic));
	if (IS_ERR(mux_chip))
		return PTR_ERR(mux_chip);

	muic = mux_chip_priv(mux_chip);
	muic->i2c = max77705->i2c_muic;
	mutex_init(&muic->lock);
	mux_chip->ops = &max77705_muic_ops;
	mux_chip->mux->states = MAX77705_MUIC_STATES;

	muic->edev = devm_extcon_dev_allocate(dev,
					      max77705_muic_extcon_cables);
	if (IS_ERR(muic->edev))
		return PTR_ERR(muic->edev);

	ret = devm_extcon_dev_register(dev, muic->edev);
	if (ret)
		return ret;

	ret = device_property_read_u32(dev, "idle-state", (u32 *)&idle_state);
	if (!ret && idle_state != MUX_IDLE_AS_IS) {
		if (idle_state < 0 || idle_state >= MAX77705_MUIC_STATES)
			return dev_err_probe(dev, -EINVAL,
					     "invalid idle state\n");
		mux_chip->mux->idle_state = idle_state;
	}

	ret = devm_mux_chip_register(dev, mux_chip);
	if (ret)
		return ret;

	dev_info(dev, "MAX77705 USB data path registered\n");
	max77705_muic_detect_boot_usb(muic);

	ret = i2c_smbus_read_byte_data(muic->i2c, MAX77705_UIC_INT);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to clear pending interrupts\n");

	ret = i2c_smbus_write_byte_data(muic->i2c, MAX77705_UIC_INT_M,
					MAX77705_UIC_INT_DRIVER_MASK);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to configure interrupts\n");

	irq = platform_get_irq_byname(pdev, "usbc");
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, NULL, max77705_muic_irq,
					IRQF_ONESHOT, "max77705-usbc", muic);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to request USBC interrupt\n");

	mutex_lock(&muic->lock);
	ret = max77705_muic_refresh_locked(muic);
	mutex_unlock(&muic->lock);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to read initial cable state\n");

	return 0;
}

static const struct of_device_id max77705_muic_of_match[] = {
	{ .compatible = "maxim,max77705-muic" },
	{ }
};
MODULE_DEVICE_TABLE(of, max77705_muic_of_match);

static struct platform_driver max77705_muic_driver = {
	.probe = max77705_muic_probe,
	.driver = {
		.name = "max77705-muic",
		.of_match_table = max77705_muic_of_match,
	},
};
module_platform_driver(max77705_muic_driver);

MODULE_DESCRIPTION("Maxim MAX77705 USB data path multiplexer");
MODULE_AUTHOR("Mathias Gluszczynski <admin@krazey.de>");
MODULE_LICENSE("GPL");
