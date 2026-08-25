// SPDX-License-Identifier: GPL-2.0
/*
 * Based on max77650-charger.c
 *
 * Copyright (C) 2025 Dzmitry Sankouski <dsankouski@gmail.org>
 *
 * Battery charger driver for MAXIM 77705 charger/power-supply.
 */

#include <linux/devm-helpers.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mfd/max77705-private.h>
#include <linux/power/max77705_charger.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>

static const char *max77705_charger_model		= "max77705";
static const char *max77705_charger_manufacturer	= "Maxim Integrated";

static const struct regmap_config max77705_chg_regmap_config = {
	.reg_base = MAX77705_CHG_REG_BASE,
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77705_CHG_REG_SAFEOUT_CTRL,
};

static enum power_supply_property max77705_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static irqreturn_t max77705_aicl_irq(int irq, void *irq_drv_data)
{
	struct max77705_charger_data *chg = irq_drv_data;
	unsigned int regval, irq_status;
	int err;

	mutex_lock(&chg->lock);

	err = regmap_read(chg->regmap, MAX77705_CHG_REG_INT_OK, &irq_status);
	if (err < 0)
		goto out;

	/*
	 * The interrupt is also raised after the decrease sequence. Stop at
	 * the hardware minimum so a repeated interrupt cannot underflow the
	 * seven-bit current field.
	 */
	while (!(irq_status & BIT(MAX77705_AICL_I))) {
		err = regmap_field_read(chg->rfield[MAX77705_CHG_CHGIN_LIM], &regval);
		if (err < 0)
			goto out;

		if (regval <= 3)
			break;

		err = regmap_field_write(chg->rfield[MAX77705_CHG_CHGIN_LIM],
					 regval - 1);
		if (err < 0)
			goto out;

		msleep(AICL_WORK_DELAY_MS);

		err = regmap_read(chg->regmap, MAX77705_CHG_REG_INT_OK, &irq_status);
		if (err < 0)
			goto out;
	}

out:
	mutex_unlock(&chg->lock);
	return IRQ_HANDLED;
}

static irqreturn_t max77705_chgin_irq(int irq, void *irq_drv_data)
{
	struct max77705_charger_data *chg = irq_drv_data;

	queue_work(chg->wqueue, &chg->chgin_work);

	return IRQ_HANDLED;
}

static const struct regmap_irq max77705_charger_irqs[] = {
	REGMAP_IRQ_REG_LINE(MAX77705_BYP_I, BITS_PER_BYTE),
	REGMAP_IRQ_REG_LINE(MAX77705_INP_LIMIT_I, BITS_PER_BYTE),
	REGMAP_IRQ_REG_LINE(MAX77705_BATP_I, BITS_PER_BYTE),
	REGMAP_IRQ_REG_LINE(MAX77705_BAT_I, BITS_PER_BYTE),
	REGMAP_IRQ_REG_LINE(MAX77705_CHG_I, BITS_PER_BYTE),
	REGMAP_IRQ_REG_LINE(MAX77705_WCIN_I, BITS_PER_BYTE),
	REGMAP_IRQ_REG_LINE(MAX77705_CHGIN_I, BITS_PER_BYTE),
	REGMAP_IRQ_REG_LINE(MAX77705_AICL_I, BITS_PER_BYTE),
};

static const struct regmap_irq_chip max77705_charger_irq_chip = {
	.name			= "max77705-charger",
	.status_base		= MAX77705_CHG_REG_INT,
	.mask_base		= MAX77705_CHG_REG_INT_MASK,
	.num_regs		= 1,
	.irqs			= max77705_charger_irqs,
	.num_irqs		= ARRAY_SIZE(max77705_charger_irqs),
};

static int max77705_charger_enable(struct max77705_charger_data *chg)
{
	int rv;

	rv = regmap_field_write(chg->rfield[MAX77705_CHG_EN],
				MAX77705_CHG_ENABLE);
	if (rv)
		dev_err(chg->dev, "unable to enable the charger: %d\n", rv);

	return rv;
}

static void max77705_charger_disable(void *data)
{
	struct max77705_charger_data *chg = data;

	cancel_delayed_work_sync(&chg->watchdog_work);

	mutex_lock(&chg->lock);
	regmap_field_write(chg->rfield[MAX77705_MODE], 0);
	regmap_update_bits(chg->regmap, MAX77705_CHG_REG_CNFG_00,
			   MAX77705_WDTEN_MASK, 0);
	regmap_field_write(chg->rfield[MAX77705_CHGINSEL],
			   MAX77705_CHGINSEL_DISABLE);
	regmap_field_write(chg->rfield[MAX77705_CHG_EN],
			   MAX77705_CHG_DISABLE);
	mutex_unlock(&chg->lock);
}

static void max77705_put_battery_info(void *data)
{
	struct max77705_charger_data *chg = data;

	power_supply_put_battery_info(chg->psy_chg, chg->bat_info);
}

static int max77705_get_online(struct regmap *regmap, int *val)
{
	unsigned int data;
	int ret;

	ret = regmap_read(regmap, MAX77705_CHG_REG_INT_OK, &data);
	if (ret < 0)
		return ret;

	*val = !!(data & MAX77705_CHGIN_OK);

	return 0;
}

static int max77705_set_input_current(struct max77705_charger_data *chg,
				      int current_ua)
{
	unsigned int maximum;
	unsigned int regval;

	if (current_ua <= 0)
		return regmap_field_write(chg->rfield[MAX77705_CHG_CHGIN_LIM], 0);

	maximum = min_t(u32, chg->input_current_max_ua,
			MAX77705_CURRENT_CHGIN_MAX);
	current_ua = clamp_val(current_ua, MAX77705_CURRENT_CHGIN_MIN,
			       maximum);
	regval = current_ua / MAX77705_CURRENT_CHGIN_STEP - 1;

	return regmap_field_write(chg->rfield[MAX77705_CHG_CHGIN_LIM],
				  regval);
}

static int max77705_set_charge_current(struct max77705_charger_data *chg,
				       int current_ua)
{
	unsigned int maximum = MAX77705_CURRENT_CHG_MAX;
	unsigned int regval;

	if (current_ua <= 0)
		return regmap_field_write(chg->rfield[MAX77705_CHG_CC_LIM], 0);

	if (chg->bat_info && chg->bat_info->constant_charge_current_max_ua > 0)
		maximum = min_t(u32, maximum,
				chg->bat_info->constant_charge_current_max_ua);

	current_ua = clamp_val(current_ua, MAX77705_CURRENT_CHGIN_MIN,
			       maximum);
	regval = current_ua / MAX77705_CURRENT_CHG_STEP;

	return regmap_field_write(chg->rfield[MAX77705_CHG_CC_LIM], regval);
}

static int max77705_b2s_overcurrent_to_reg(unsigned int current_ua,
					   unsigned int *regval)
{
	if (!current_ua) {
		*regval = MAX77705_B2SOVRC_DISABLE;
		return 0;
	}

	if (current_ua < MAX77705_B2SOVRC_MIN_UA ||
	    current_ua > MAX77705_B2SOVRC_MAX_UA ||
	    (current_ua - MAX77705_B2SOVRC_MIN_UA) %
		    MAX77705_B2SOVRC_STEP_UA)
		return -EINVAL;

	*regval = MAX77705_B2SOVRC_MIN_REG +
		(current_ua - MAX77705_B2SOVRC_MIN_UA) /
		MAX77705_B2SOVRC_STEP_UA;

	return 0;
}

static int max77705_parse_current_table(struct max77705_charger_data *chg)
{
	struct device *dev = chg->dev;
	int count;
	int ret;
	int i;

	if (!device_property_present(dev, "maxim,charging-current-table"))
		return 0;

	count = device_property_count_u32(dev, "maxim,charging-current-table");
	if (count <= 0 || count % 3)
		return dev_err_probe(dev, -EINVAL,
				     "charging current table must use triplets\n");

	chg->current_table_size = count / 3;
	chg->current_table = devm_kcalloc(dev, chg->current_table_size,
					  sizeof(*chg->current_table),
					  GFP_KERNEL);
	if (!chg->current_table)
		return -ENOMEM;

	ret = device_property_read_u32_array(dev,
					     "maxim,charging-current-table",
					     (u32 *)chg->current_table, count);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to read charging current table\n");

	for (i = 0; i < chg->current_table_size; i++) {
		const struct max77705_current_entry *entry =
			&chg->current_table[i];

		if (entry->input_current_ua < MAX77705_CURRENT_CHGIN_MIN ||
		    entry->input_current_ua > chg->input_current_max_ua ||
		    entry->charge_current_ua < MAX77705_CURRENT_CHGIN_MIN ||
		    entry->charge_current_ua > MAX77705_CURRENT_CHG_MAX)
			return dev_err_probe(dev, -EINVAL,
					     "invalid current table entry %d\n", i);
	}

	return 0;
}

static int max77705_read_u32_property(struct device *dev,
				      const char *name, u32 *value)
{
	int ret;

	if (!device_property_present(dev, name))
		return 0;

	ret = device_property_read_u32(dev, name, value);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read %s\n", name);

	return 0;
}

static int max77705_charger_parse_properties(struct max77705_charger_data *chg)
{
	unsigned int regval;
	int ret;

	chg->default_input_current_ua = MAX77705_STOCK_SAFE_CURRENT;
	chg->default_charge_current_ua = MAX77705_STOCK_SAFE_CURRENT;
	chg->input_current_max_ua = MAX77705_CURRENT_CHGIN_MAX;
	chg->b2s_ocp_ua = 4800000;
	chg->watchdog_enabled =
		device_property_read_bool(chg->dev, "maxim,enable-watchdog");

	ret = max77705_read_u32_property(chg->dev,
					 "maxim,default-input-current-microamp",
					 &chg->default_input_current_ua);
	if (ret)
		return ret;

	ret = max77705_read_u32_property(chg->dev,
					 "maxim,default-charge-current-microamp",
					 &chg->default_charge_current_ua);
	if (ret)
		return ret;

	ret = max77705_read_u32_property(chg->dev,
					 "maxim,input-current-max-microamp",
					 &chg->input_current_max_ua);
	if (ret)
		return ret;

	ret = max77705_read_u32_property(chg->dev,
					 "maxim,b2s-overcurrent-microamp",
					 &chg->b2s_ocp_ua);
	if (ret)
		return ret;

	if (chg->input_current_max_ua < MAX77705_CURRENT_CHGIN_MIN ||
	    chg->input_current_max_ua > MAX77705_CURRENT_CHGIN_MAX ||
	    chg->default_input_current_ua < MAX77705_CURRENT_CHGIN_MIN ||
	    chg->default_input_current_ua > chg->input_current_max_ua ||
	    chg->default_charge_current_ua < MAX77705_CURRENT_CHGIN_MIN ||
	    chg->default_charge_current_ua > MAX77705_CURRENT_CHG_MAX)
		return dev_err_probe(chg->dev, -EINVAL,
				     "invalid default charging currents\n");

	ret = max77705_b2s_overcurrent_to_reg(chg->b2s_ocp_ua, &regval);
	if (ret)
		return dev_err_probe(chg->dev, ret,
				     "invalid BAT-to-SYS overcurrent limit\n");

	return max77705_parse_current_table(chg);
}

static void max77705_get_policy_currents(struct max77705_charger_data *chg,
					 u32 *input_ua, u32 *charge_ua)
{
	u32 cable_type = chg->policy_selected ? chg->cable_type :
		MAX77705_STOCK_SAFE_CABLE_TYPE;
	unsigned int i;

	for (i = 0; i < chg->current_table_size; i++) {
		if (chg->current_table[i].cable_type != cable_type)
			continue;

		*input_ua = chg->current_table[i].input_current_ua;
		*charge_ua = chg->current_table[i].charge_current_ua;
		return;
	}

	if (!chg->policy_selected) {
		*input_ua = MAX77705_STOCK_SAFE_CURRENT;
		*charge_ua = MAX77705_STOCK_SAFE_CURRENT;
		return;
	}

	*input_ua = chg->default_input_current_ua;
	*charge_ua = chg->default_charge_current_ua;
}

static void max77705_watchdog_work(struct work_struct *work)
{
	struct max77705_charger_data *chg =
		container_of(to_delayed_work(work),
			     struct max77705_charger_data, watchdog_work);
	unsigned int mode;
	int online;
	int ret;

	mutex_lock(&chg->lock);

	ret = max77705_get_online(chg->regmap, &online);
	if (ret || !online || !chg->watchdog_enabled)
		goto out;

	ret = regmap_field_read(chg->rfield[MAX77705_MODE], &mode);
	if (ret || !(mode & MAX77705_CHG_MASK))
		goto out;

	ret = regmap_update_bits(chg->regmap, MAX77705_CHG_REG_CNFG_06,
				 MAX77705_WDTCLR_MASK, MAX77705_WDTCLR);
	if (!ret)
		queue_delayed_work(chg->wqueue, &chg->watchdog_work,
				   msecs_to_jiffies(MAX77705_WATCHDOG_INTERVAL_MS));

out:
	mutex_unlock(&chg->lock);
}

static int max77705_set_watchdog_locked(struct max77705_charger_data *chg,
					bool enable)
{
	int ret;

	cancel_delayed_work(&chg->watchdog_work);

	ret = regmap_update_bits(chg->regmap, MAX77705_CHG_REG_CNFG_00,
				 MAX77705_WDTEN_MASK,
				 enable ? MAX77705_WDTEN_MASK : 0);
	if (ret || !enable)
		return ret;

	ret = regmap_update_bits(chg->regmap, MAX77705_CHG_REG_CNFG_06,
				 MAX77705_WDTCLR_MASK, MAX77705_WDTCLR);
	if (ret)
		return ret;

	mod_delayed_work(chg->wqueue, &chg->watchdog_work,
			 msecs_to_jiffies(MAX77705_WATCHDOG_INTERVAL_MS));

	return 0;
}

static int max77705_charger_sync_locked(struct max77705_charger_data *chg)
{
	u32 input_ua;
	u32 charge_ua;
	int online;
	int ret;

	ret = max77705_get_online(chg->regmap, &online);
	if (ret)
		return ret;

	if (!online) {
		chg->cable_type = 0;
		chg->policy_selected = false;
	}

	if (!online || (chg->policy_selected && chg->cable_type <= 1)) {
		ret = regmap_field_write(chg->rfield[MAX77705_MODE], 0);
		if (ret)
			return ret;

		ret = max77705_set_watchdog_locked(chg, false);
		if (ret)
			return ret;

		return regmap_field_write(chg->rfield[MAX77705_CHGINSEL],
					  MAX77705_CHGINSEL_DISABLE);
	}

	max77705_get_policy_currents(chg, &input_ua, &charge_ua);

	ret = regmap_field_write(chg->rfield[MAX77705_CHGINSEL],
				 MAX77705_CHGINSEL_ENABLE);
	if (ret)
		goto disable;

	ret = max77705_set_input_current(chg, input_ua);
	if (ret)
		goto disable;

	ret = max77705_set_charge_current(chg, charge_ua);
	if (ret)
		goto disable;

	ret = regmap_field_write(chg->rfield[MAX77705_MODE],
				 MAX77705_CHG_MASK | MAX77705_BUCK_MASK);
	if (ret)
		goto disable;

	ret = max77705_set_watchdog_locked(chg, chg->watchdog_enabled);
	if (ret)
		goto disable;

	dev_info(chg->dev,
		 "input present, cable %u, limits %u/%u uA\n",
		 chg->policy_selected ? chg->cable_type :
			MAX77705_STOCK_SAFE_CABLE_TYPE,
		 input_ua, charge_ua);

	return 0;

disable:
	regmap_field_write(chg->rfield[MAX77705_MODE], 0);
	max77705_set_watchdog_locked(chg, false);
	return ret;
}

static int max77705_check_battery(struct max77705_charger_data *chg, int *val)
{
	unsigned int reg_data;
	unsigned int reg_data2;
	struct regmap *regmap = chg->regmap;

	regmap_read(regmap, MAX77705_CHG_REG_INT_OK, &reg_data);

	dev_dbg(chg->dev, "CHG_INT_OK(0x%x)\n", reg_data);

	regmap_read(regmap, MAX77705_CHG_REG_DETAILS_00, &reg_data2);

	dev_dbg(chg->dev, "CHG_DETAILS00(0x%x)\n", reg_data2);

	if ((reg_data & MAX77705_BATP_OK) || !(reg_data2 & MAX77705_BATP_DTLS))
		*val = true;
	else
		*val = false;

	return 0;
}

static int max77705_get_charge_type(struct max77705_charger_data *chg, int *val)
{
	struct regmap *regmap = chg->regmap;
	unsigned int reg_data;
	unsigned int mode;
	int ret;

	ret = regmap_field_read(chg->rfield[MAX77705_MODE], &mode);
	if (ret)
		return ret;
	if (!(mode & MAX77705_CHG_MASK)) {
		*val = POWER_SUPPLY_CHARGE_TYPE_NONE;
		return 0;
	}

	regmap_read(regmap, MAX77705_CHG_REG_DETAILS_01, &reg_data);
	reg_data &= MAX77705_CHG_DTLS;

	switch (reg_data) {
	case 0x0:
	case MAX77705_CHARGER_CONSTANT_CURRENT:
	case MAX77705_CHARGER_CONSTANT_VOLTAGE:
		*val = POWER_SUPPLY_CHARGE_TYPE_FAST;
		return 0;
	default:
		*val = POWER_SUPPLY_CHARGE_TYPE_NONE;
		return 0;
	}

	return 0;
}

static int max77705_get_status(struct max77705_charger_data *chg, int *val)
{
	struct regmap *regmap = chg->regmap;
	unsigned int reg_data;
	unsigned int mode;
	int online;
	int ret;

	ret = regmap_field_read(chg->rfield[MAX77705_MODE], &mode);
	if (ret)
		return ret;
	if (!(mode & MAX77705_CHG_MASK)) {
		ret = max77705_get_online(regmap, &online);
		if (ret)
			return ret;
		*val = online ? POWER_SUPPLY_STATUS_NOT_CHARGING :
			POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	}

	regmap_read(regmap, MAX77705_CHG_REG_DETAILS_01, &reg_data);
	reg_data &= MAX77705_CHG_DTLS;

	switch (reg_data) {
	case 0x0:
	case MAX77705_CHARGER_CONSTANT_CURRENT:
	case MAX77705_CHARGER_CONSTANT_VOLTAGE:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		return 0;
	case MAX77705_CHARGER_END_OF_CHARGE:
	case MAX77705_CHARGER_DONE:
		*val = POWER_SUPPLY_STATUS_FULL;
		return 0;
	/* those values hard coded as in vendor kernel, because of */
	/* failure to determine it's actual meaning. */
	case 0x05:
	case 0x06:
	case 0x07:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	case 0x08:
	case 0xA:
	case 0xB:
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
		return 0;
	}

	return 0;
}

static int max77705_get_vbus_state(struct regmap *regmap, int *value)
{
	int ret;
	unsigned int charge_dtls;

	ret = regmap_read(regmap, MAX77705_CHG_REG_DETAILS_00, &charge_dtls);
	if (ret)
		return ret;

	charge_dtls = ((charge_dtls & MAX77705_CHGIN_DTLS) >>
			MAX77705_CHGIN_DTLS_SHIFT);

	switch (charge_dtls) {
	case 0x00:
		*value = POWER_SUPPLY_HEALTH_UNDERVOLTAGE;
		break;
	case 0x01:
		*value = POWER_SUPPLY_HEALTH_UNDERVOLTAGE;
		break;
	case 0x02:
		*value = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;
	case 0x03:
		*value = POWER_SUPPLY_HEALTH_GOOD;
		break;
	default:
		return 0;
	}
	return 0;
}

static int max77705_get_battery_health(struct max77705_charger_data *chg,
					int *value)
{
	struct regmap *regmap = chg->regmap;
	unsigned int bat_dtls;

	regmap_read(regmap, MAX77705_CHG_REG_DETAILS_01, &bat_dtls);
	*value = POWER_SUPPLY_HEALTH_GOOD;
	bat_dtls = ((bat_dtls & MAX77705_BAT_DTLS) >> MAX77705_BAT_DTLS_SHIFT);

	switch (bat_dtls) {
	case MAX77705_BATTERY_NOBAT:
		dev_dbg(chg->dev, "%s: No battery and the chg is suspended\n",
			__func__);
		*value = POWER_SUPPLY_HEALTH_NO_BATTERY;
		break;
	case MAX77705_BATTERY_PREQUALIFICATION:
		dev_dbg(chg->dev, "%s: battery is okay but its voltage is low(~VPQLB)\n",
			__func__);
		break;
	case MAX77705_BATTERY_DEAD:
		dev_dbg(chg->dev, "%s: battery dead\n", __func__);
		*value = POWER_SUPPLY_HEALTH_DEAD;
		break;
	case MAX77705_BATTERY_GOOD:
	case MAX77705_BATTERY_LOWVOLTAGE:
		*value = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case MAX77705_BATTERY_OVERVOLTAGE:
		dev_dbg(chg->dev, "%s: battery ovp\n", __func__);
		*value = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;
	default:
		dev_dbg(chg->dev, "%s: battery unknown\n", __func__);
		*value = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;
	}

	return 0;
}

static int max77705_get_health(struct max77705_charger_data *chg, int *val)
{
	struct regmap *regmap = chg->regmap;
	int ret, is_online = 0;

	ret = max77705_get_online(regmap, &is_online);
	if (ret)
		return ret;
	if (is_online) {
		ret = max77705_get_vbus_state(regmap, val);
		if (ret || (*val != POWER_SUPPLY_HEALTH_GOOD))
			return ret;
	}
	return max77705_get_battery_health(chg, val);
}

static int max77705_get_input_current(struct max77705_charger_data *chg,
					int *val)
{
	unsigned int reg_data;
	int get_current = 0;

	regmap_field_read(chg->rfield[MAX77705_CHG_CHGIN_LIM], &reg_data);

	if (reg_data <= 3)
		get_current = MAX77705_CURRENT_CHGIN_MIN;
	else
		get_current = (reg_data + 1) * MAX77705_CURRENT_CHGIN_STEP;

	*val = get_current;

	return 0;
}

static int max77705_get_charge_current(struct max77705_charger_data *chg,
					int *val)
{
	unsigned int reg_data;

	regmap_field_read(chg->rfield[MAX77705_CHG_CC_LIM], &reg_data);

	*val = reg_data <= 0x2 ? MAX77705_CURRENT_CHGIN_MIN : reg_data * MAX77705_CURRENT_CHG_STEP;

	return 0;
}

static int max77705_set_float_voltage(struct max77705_charger_data *chg,
					int float_voltage)
{
	int float_voltage_mv;
	unsigned int reg_data = 0;

	float_voltage_mv = float_voltage / 1000;
	reg_data = float_voltage_mv <= 4000 ? 0x0 :
		float_voltage_mv >= 4500 ? 0x23 :
		(float_voltage_mv <= 4200) ? (float_voltage_mv - 4000) / 50 :
		(((float_voltage_mv - 4200) / 10) + 0x04);

	return regmap_field_write(chg->rfield[MAX77705_CHG_CV_PRM], reg_data);
}

static int max77705_get_float_voltage(struct max77705_charger_data *chg,
					int *val)
{
	unsigned int reg_data = 0;
	int voltage_mv;

	regmap_field_read(chg->rfield[MAX77705_CHG_CV_PRM], &reg_data);
	voltage_mv = reg_data <= 0x04 ? reg_data * 50 + 4000 :
					(reg_data - 4) * 10 + 4200;
	*val = voltage_mv * 1000;

	return 0;
}

static int max77705_chg_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct max77705_charger_data *chg = power_supply_get_drvdata(psy);
	struct regmap *regmap = chg->regmap;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		return max77705_get_online(regmap, &val->intval);
	case POWER_SUPPLY_PROP_PRESENT:
		return max77705_check_battery(chg, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return max77705_get_status(chg, &val->intval);
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		return max77705_get_charge_type(chg, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return max77705_get_health(chg, &val->intval);
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return max77705_get_input_current(chg, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return max77705_get_charge_current(chg, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return max77705_get_float_voltage(chg, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = chg->bat_info->voltage_max_design_uv;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = max77705_charger_model;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = max77705_charger_manufacturer;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int max77705_set_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 const union power_supply_propval *val)
{
	struct max77705_charger_data *chg = power_supply_get_drvdata(psy);
	int err;

	mutex_lock(&chg->lock);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (val->intval < 0) {
			err = -EINVAL;
			break;
		}
		chg->cable_type = val->intval;
		chg->policy_selected = true;
		err = max77705_charger_sync_locked(chg);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		err = max77705_set_charge_current(chg, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		err = max77705_set_input_current(chg, val->intval);
		break;
	default:
		err = -EINVAL;
	}
	mutex_unlock(&chg->lock);

	if (!err)
		power_supply_changed(chg->psy_chg);

	return err;
}

static int max77705_property_is_writeable(struct power_supply *psy,
					  enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return true;
	default:
		return false;
	}
}

static const struct power_supply_desc max77705_charger_psy_desc = {
	.name = "max77705-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = max77705_charger_props,
	.property_is_writeable = max77705_property_is_writeable,
	.num_properties = ARRAY_SIZE(max77705_charger_props),
	.get_property = max77705_chg_get_property,
	.set_property = max77705_set_property,
};

static void max77705_chgin_isr_work(struct work_struct *work)
{
	struct max77705_charger_data *chg =
		container_of(work, struct max77705_charger_data, chgin_work);

	mutex_lock(&chg->lock);
	if (max77705_charger_sync_locked(chg))
		dev_err(chg->dev, "failed to update charger input state\n");
	mutex_unlock(&chg->lock);

	power_supply_changed(chg->psy_chg);
}

static int max77705_charger_initialize(struct max77705_charger_data *chg)
{
	struct power_supply_battery_info *info;
	struct regmap *regmap = chg->regmap;
	unsigned int reg_data;
	int err;

	err = power_supply_get_battery_info(chg->psy_chg, &info);
	if (err)
		return dev_err_probe(chg->dev, err, "error on getting battery info");

	chg->bat_info = info;

	err = devm_add_action_or_reset(chg->dev, max77705_put_battery_info,
				       chg);
	if (err)
		return err;

	/* unlock charger setting protect */
	/* slowest LX slope */
	err = regmap_field_write(chg->rfield[MAX77705_CHGPROT], MAX77705_CHGPROT_UNLOCKED);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_LX_SLOPE], MAX77705_SLOWEST_LX_SLOPE);
	if (err)
		goto err;

	/* fast charge timer disable */
	/* restart threshold disable */
	/* pre-qual charge disable */
	err = regmap_field_write(chg->rfield[MAX77705_FCHGTIME], MAX77705_FCHGTIME_DISABLE);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_CHG_RSTRT], MAX77705_CHG_RSTRT_DISABLE);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_CHG_PQEN],
				 MAX77705_CHG_PQEN_DISABLE);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_RECYCLE_EN],
				 MAX77705_RECYCLE_EN_ENABLE);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_MODE], 0);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_CHGINSEL],
				 MAX77705_CHGINSEL_DISABLE);
	if (err)
		goto err;

	/* charge current 450mA(default) */
	/* otg current limit 900mA */
	err = regmap_field_write(chg->rfield[MAX77705_OTG_ILIM], MAX77705_OTG_ILIM_900);
	if (err)
		goto err;

	/* BAT to SYS overcurrent protection */
	err = max77705_b2s_overcurrent_to_reg(chg->b2s_ocp_ua, &reg_data);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_REG_B2SOVRC], reg_data);
	if (err)
		goto err;

	/* top off current 150mA */
	/* top off timer 30min */
	err = regmap_field_write(chg->rfield[MAX77705_TO], MAX77705_TO_ITH_150MA);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_TO_TIME], MAX77705_TO_TIME_30M);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_SYS_TRACK], MAX77705_SYS_TRACK_DISABLE);
	if (err)
		goto err;

	/* cv voltage 4.2V or 4.35V */
	if (info->constant_charge_voltage_max_uv > 0)
		err = max77705_set_float_voltage(chg,
						 info->constant_charge_voltage_max_uv);
	else if (info->voltage_max_design_uv > 0)
		err = max77705_set_float_voltage(chg,
						 info->voltage_max_design_uv);
	else
		err = max77705_set_float_voltage(chg, 4200000);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_VCHGIN], MAX77705_VCHGIN_4_5);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_WCIN], MAX77705_WCIN_4_5);
	if (err)
		goto err;

	/* Factory boost remains available; the fuel gauge uses its normal path. */
	err = regmap_field_write(chg->rfield[MAX77705_REG_FMBST],
				 MAX77705_FMBST_ENABLE);
	if (err)
		goto err;

	err = regmap_field_write(chg->rfield[MAX77705_REG_FGSRC],
				 MAX77705_FGSRC_NORMAL);
	if (err)
		goto err;

	err = regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_00,
				 MAX77705_WDTEN_MASK, 0);
	if (err)
		goto err;

	/* VBYPSET=5.0V */
	err = regmap_field_write(chg->rfield[MAX77705_VBYPSET], 0);
	if (err)
		goto err;

	/* Switching Frequency : 1.5MHz */
	err = regmap_field_write(chg->rfield[MAX77705_REG_FSW], MAX77705_CHG_FSW_1_5MHz);
	if (err)
		goto err;

	/* Auto skip mode */
	err = regmap_field_write(chg->rfield[MAX77705_REG_DISKIP], MAX77705_AUTO_SKIP);
	if (err)
		goto err;

	return 0;

err:
	return dev_err_probe(chg->dev, err, "error while configuring");

}

static int max77705_charger_probe(struct i2c_client *i2c)
{
	struct power_supply_config pscfg = {};
	struct max77705_charger_data *chg;
	struct regmap_irq_chip *chip_desc;
	struct device *dev;
	struct regmap_irq_chip_data *irq_data;
	int sync_ret;
	int ret;

	dev = &i2c->dev;

	chg = devm_kzalloc(dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	chg->dev = dev;
	i2c_set_clientdata(i2c, chg);
	mutex_init(&chg->lock);

	ret = max77705_charger_parse_properties(chg);
	if (ret)
		return ret;

	chip_desc = devm_kmemdup(dev, &max77705_charger_irq_chip,
				 sizeof(max77705_charger_irq_chip),
				 GFP_KERNEL);
	if (!chip_desc)
		return -ENOMEM;
	chip_desc->irq_drv_data = chg;

	chg->regmap = devm_regmap_init_i2c(i2c, &max77705_chg_regmap_config);
	if (IS_ERR(chg->regmap))
		return PTR_ERR(chg->regmap);

	for (int i = 0; i < MAX77705_N_REGMAP_FIELDS; i++) {
		chg->rfield[i] = devm_regmap_field_alloc(dev, chg->regmap,
							 max77705_reg_field[i]);
		if (IS_ERR(chg->rfield[i]))
			return dev_err_probe(dev, PTR_ERR(chg->rfield[i]),
					     "cannot allocate regmap field\n");
	}

	pscfg.fwnode = dev_fwnode(dev);
	pscfg.drv_data = chg;

	chg->psy_chg = devm_power_supply_register(dev, &max77705_charger_psy_desc, &pscfg);
	if (IS_ERR(chg->psy_chg))
		return PTR_ERR(chg->psy_chg);

	ret = devm_regmap_add_irq_chip(chg->dev, chg->regmap, i2c->irq,
					IRQF_ONESHOT, 0,
					chip_desc, &irq_data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add irq chip\n");

	chg->wqueue = devm_alloc_ordered_workqueue(dev, "%s", 0, dev_name(dev));
	if (!chg->wqueue)
		return -ENOMEM;

	ret = devm_work_autocancel(dev, &chg->chgin_work,
				   max77705_chgin_isr_work);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to initialize interrupt work\n");

	ret = devm_delayed_work_autocancel(dev, &chg->watchdog_work,
					   max77705_watchdog_work);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to initialize watchdog work\n");

	ret = max77705_charger_initialize(chg);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize charger IC\n");

	ret = devm_request_threaded_irq(dev, regmap_irq_get_virq(irq_data, MAX77705_CHGIN_I),
					NULL, max77705_chgin_irq,
					IRQF_TRIGGER_NONE,
					"chgin-irq", chg);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(dev, regmap_irq_get_virq(irq_data, MAX77705_AICL_I),
					NULL, max77705_aicl_irq,
					IRQF_TRIGGER_NONE,
					"aicl-irq", chg);
	if (ret)
		return ret;

	ret = max77705_charger_enable(chg);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable charge\n");

	ret = devm_add_action_or_reset(dev, max77705_charger_disable, chg);
	if (ret)
		return ret;

	mutex_lock(&chg->lock);
	sync_ret = max77705_charger_sync_locked(chg);
	mutex_unlock(&chg->lock);
	if (sync_ret)
		return dev_err_probe(dev, sync_ret,
				     "failed to select initial charging policy\n");

	return 0;
}

static const struct of_device_id max77705_charger_of_match[] = {
	{ .compatible = "maxim,max77705-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, max77705_charger_of_match);

static struct i2c_driver max77705_charger_driver = {
	.driver = {
		.name = "max77705-charger",
		.of_match_table = max77705_charger_of_match,
	},
	.probe = max77705_charger_probe,
};
module_i2c_driver(max77705_charger_driver);

MODULE_AUTHOR("Dzmitry Sankouski <dsankouski@gmail.com>");
MODULE_DESCRIPTION("Maxim MAX77705 charger driver");
MODULE_LICENSE("GPL");
