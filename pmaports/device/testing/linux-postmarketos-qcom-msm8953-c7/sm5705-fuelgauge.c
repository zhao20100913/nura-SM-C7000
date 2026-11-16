// SPDX-License-Identifier: GPL-2.0-only
/*
 * Silicon Mitus SM5705 fuel gauge (Samsung Galaxy C7, SM-C7000)
 *
 * The gauge lives at I2C address 0x71 on the same bit-banged bus as the
 * charger and exposes 16-bit big-endian registers.  Only the three values
 * userspace needs are decoded here; the Samsung coulomb-counting and OCV
 * compensation logic is deliberately not ported, the chip keeps running
 * its own firmware and reports a state of charge in permille.
 *
 * Register decoding follows the downstream Samsung driver
 * drivers/battery/sm5705_fuelgauge.c (GPL-2.0).
 */

#include <linux/i2c.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>

#define SM5705_FG_REG_DEVICE_ID	0x00
#define SM5705_FG_REG_SOC	0x05
#define SM5705_FG_REG_VOLTAGE	0x07
#define SM5705_FG_REG_TEMPERATURE	0x09

struct sm5705_fg {
	struct device *dev;
	struct i2c_client *client;
	struct power_supply *psy;
};

static int sm5705_fg_read16(struct sm5705_fg *fg, u8 reg, u16 *val)
{
	struct i2c_msg msgs[2];
	u8 buf[2];
	int ret;

	msgs[0].addr = fg->client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg;

	msgs[1].addr = fg->client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 2;
	msgs[1].buf = buf;

	ret = i2c_transfer(fg->client->adapter, msgs, 2);
	if (ret != 2)
		return ret < 0 ? ret : -EIO;

	*val = ((u16)buf[0] << 8) | buf[1];
	return 0;
}

static int sm5705_fg_soc_permille(struct sm5705_fg *fg)
{
	u16 v;
	int ret = sm5705_fg_read16(fg, SM5705_FG_REG_SOC, &v);

	if (ret)
		return ret;

	/* integer part * 10, fractional part * 10 / 256 */
	return ((v & 0xff00) >> 8) * 10 + ((v & 0x00ff) * 10) / 256;
}

static int sm5705_fg_voltage_mv(struct sm5705_fg *fg)
{
	u16 v;
	int ret = sm5705_fg_read16(fg, SM5705_FG_REG_VOLTAGE, &v);

	if (ret)
		return ret;

	/* 3 integer bits (1000 mV/LSB), 11 fractional bits (1000/2048 mV) */
	return ((v & 0x3800) >> 11) * 1000 + ((v & 0x07ff) * 1000) / 2048;
}

static int sm5705_fg_temp_tenths(struct sm5705_fg *fg)
{
	u16 v;
	int ret = sm5705_fg_read16(fg, SM5705_FG_REG_TEMPERATURE, &v);
	int temp;

	if (ret)
		return ret;

	temp = ((v & 0x7f00) >> 8) * 10 + ((v & 0x00f0) * 10) / 256;
	if (v & 0x8000)
		temp = -temp;

	return temp;
}

static int sm5705_fg_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct sm5705_fg *fg = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = sm5705_fg_soc_permille(fg);
		if (ret < 0)
			return ret;
		val->intval = clamp(ret / 10, 0, 100);
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = sm5705_fg_voltage_mv(fg);
		if (ret < 0)
			return ret;
		val->intval = ret * 1000;
		return 0;
	case POWER_SUPPLY_PROP_TEMP:
		ret = sm5705_fg_temp_tenths(fg);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		return 0;
	default:
		return -EINVAL;
	}
}

static enum power_supply_property sm5705_fg_props[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_PRESENT,
};

static const struct power_supply_desc sm5705_fg_desc = {
	.name		= "sm5705-fuelgauge",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= sm5705_fg_props,
	.num_properties	= ARRAY_SIZE(sm5705_fg_props),
	.get_property	= sm5705_fg_get_property,
};

static int sm5705_fg_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {};
	struct sm5705_fg *fg;
	u16 id = 0;
	int ret;

	fg = devm_kzalloc(&client->dev, sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return -ENOMEM;

	fg->dev = &client->dev;
	fg->client = client;

	ret = sm5705_fg_read16(fg, SM5705_FG_REG_DEVICE_ID, &id);
	if (ret)
		return dev_err_probe(fg->dev, ret, "cannot read DEVICE_ID\n");

	dev_info(fg->dev, "SM5705 fuel gauge, ID 0x%04x, soc=%d%% vbat=%dmV\n",
		 id, sm5705_fg_soc_permille(fg) / 10,
		 sm5705_fg_voltage_mv(fg));

	psy_cfg.drv_data = fg;
	psy_cfg.of_node = client->dev.of_node;
	fg->psy = devm_power_supply_register(fg->dev, &sm5705_fg_desc,
					     &psy_cfg);
	if (IS_ERR(fg->psy))
		return dev_err_probe(fg->dev, PTR_ERR(fg->psy),
				     "cannot register power supply\n");

	i2c_set_clientdata(client, fg);

	return 0;
}

static const struct of_device_id sm5705_fg_of_match[] = {
	{ .compatible = "siliconmitus,sm5705-fuelgauge" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sm5705_fg_of_match);

static struct i2c_driver sm5705_fg_driver = {
	.driver = {
		.name = "sm5705-fuelgauge",
		.of_match_table = sm5705_fg_of_match,
	},
	.probe = sm5705_fg_probe,
};
module_i2c_driver(sm5705_fg_driver);

MODULE_DESCRIPTION("Silicon Mitus SM5705 fuel gauge");
MODULE_LICENSE("GPL");
