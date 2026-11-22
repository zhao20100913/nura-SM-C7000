// SPDX-License-Identifier: GPL-2.0-only
/*
 * Silicon Mitus SM5705 charger (Samsung Galaxy C7, SM-C7000)
 *
 * The SM5705 combines a MUIC, a switching charger and flash LED drivers
 * behind one I2C address (0x49).  The C7 wires it on a bit-banged bus
 * (SDA gpio98 / SCL gpio99) with the charger interrupt on gpio61.
 *
 * This driver only covers what a phone needs to charge and to report the
 * supply state to userspace: VBUS detection, input/charge current limits,
 * float voltage and the charge status bits.  MUIC role switching, OTG and
 * the flash LEDs stay unimplemented.
 *
 * Register map and encodings are taken from the downstream Samsung driver
 * drivers/battery/sm5705_charger.c (GPL-2.0) of the C7 kernel.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/minmax.h>
#include <linux/bits.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#define SM5705_REG_INT1		0x00
#define SM5705_REG_INT2		0x01
#define SM5705_REG_STATUS1	0x08
#define SM5705_REG_STATUS2	0x09
#define SM5705_REG_STATUS3	0x0a
#define SM5705_REG_CNTL		0x0c
#define SM5705_REG_VBUSCNTL	0x0d
#define SM5705_REG_CHGCNTL1	0x0f
#define SM5705_REG_CHGCNTL2	0x10
#define SM5705_REG_CHGCNTL3	0x11
#define SM5705_REG_CHGCNTL4	0x12
#define SM5705_REG_CHGCNTL8	0x16
#define SM5705_REG_DEVICEID	0x37

/* CNTL[2:0]: charger operation mode */
#define SM5705_OP_MODE_SUSPEND	0x0
#define SM5705_OP_MODE_CHG_OFF	0x4
#define SM5705_OP_MODE_CHG_ON	0x5

/* STATUS1[0] */
#define SM5705_STATUS1_VBUSPOK	BIT(0)

/* STATUS2 */
#define SM5705_STATUS2_CHGON	BIT(3)
#define SM5705_STATUS2_TOPOFF	BIT(5)
#define SM5705_STATUS2_DONE	BIT(6)

/* CHGCNTL4[6] = automatic stop, [5:0] = float voltage */
#define SM5705_CHGCNTL4_AUTOSTOP	BIT(6)

/* CHGCNTL8[7] reloads the charge watchdog timer */
#define SM5705_CHGCNTL8_WDT_RST		BIT(7)

/* The chip has interrupt mask registers we do not know how to program, so a
 * slow poll keeps VBUS detection working even when no IRQ ever arrives.
 */
#define SM5705_POLL_MS			5000

struct sm5705_charger {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply *psy;
	struct delayed_work work;
	int irq;
	bool vbus;
	bool charging;
};

static int sm5705_read(struct sm5705_charger *chg, u8 reg, u8 *val)
{
	unsigned int v;
	int ret = regmap_read(chg->regmap, reg, &v);

	if (ret)
		return ret;
	*val = v & 0xff;
	return 0;
}

static int sm5705_write(struct sm5705_charger *chg, u8 reg, u8 val)
{
	return regmap_write(chg->regmap, reg, val);
}

/* (mA - 100) / 25, clamped to 3.275 A (downstream encoding) */
static u8 sm5705_input_limit_offset(unsigned int ma)
{
	if (ma < 100)
		return 0x10;		/* suspend */
	ma = min(ma, 3275u);
	return ((ma - 100) / 25) & 0x7f;
}

/* (mA - 100) / 50, clamped to 3.25 A */
static u8 sm5705_fastchg_offset(unsigned int ma)
{
	if (ma < 200)
		return 0x02;
	ma = min(ma, 3250u);
	return ((ma - 100) / 50) & 0x3f;
}

/* 4.01 V .. 4.62 V in 10 mV steps starting at offset 2 */
static u8 sm5705_batreg_offset(unsigned int mv)
{
	if (mv < 4000)
		return 0x0;
	if (mv < 4010)
		return 0x1;
	if (mv < 4630)
		return ((mv - 4010) / 10) + 2;
	return 0x15;
}

static int vbus_limit_ma = 500;
module_param(vbus_limit_ma, int, 0644);
MODULE_PARM_DESC(vbus_limit_ma, "VBUS input current limit in mA");

static int fastchg_ma = 500;
module_param(fastchg_ma, int, 0644);
MODULE_PARM_DESC(fastchg_ma, "fast charge current in mA");

static int float_mv = 4200;
module_param(float_mv, int, 0644);
MODULE_PARM_DESC(float_mv, "battery float voltage in mV");

static bool enable_charging = true;
module_param(enable_charging, bool, 0644);
MODULE_PARM_DESC(enable_charging, "enable the charger when VBUS is present");

static void sm5705_dump_regs(struct sm5705_charger *chg, const char *why)
{
	u8 r[4];
	int i;

	for (i = 0; i < 4; i++)
		sm5705_read(chg, SM5705_REG_STATUS1 + i, &r[i]);

	dev_info(chg->dev, "%s: st1=%02x st2=%02x st3=%02x st4=%02x vbus=%d\n",
		 why, r[0], r[1], r[2], r[3], chg->vbus);
}

static void sm5705_update_state(struct sm5705_charger *chg)
{
	u8 st1 = 0, st2 = 0;

	sm5705_read(chg, SM5705_REG_STATUS1, &st1);
	sm5705_read(chg, SM5705_REG_STATUS2, &st2);

	chg->vbus = !!(st1 & SM5705_STATUS1_VBUSPOK);
	chg->charging = !!(st2 & SM5705_STATUS2_CHGON);
}

static int sm5705_configure(struct sm5705_charger *chg)
{
	u8 cntl;
	int ret;

	ret = sm5705_write(chg, SM5705_REG_VBUSCNTL,
			   sm5705_input_limit_offset(vbus_limit_ma));
	if (ret)
		return ret;

	ret = sm5705_write(chg, SM5705_REG_CHGCNTL2,
			   sm5705_fastchg_offset(fastchg_ma));
	if (ret)
		return ret;

	/* float voltage + automatic stop */
	ret = sm5705_write(chg, SM5705_REG_CHGCNTL4,
			   sm5705_batreg_offset(float_mv) | SM5705_CHGCNTL4_AUTOSTOP);
	if (ret)
		return ret;

	if (!enable_charging)
		return 0;

	cntl = chg->vbus ? SM5705_OP_MODE_CHG_ON : SM5705_OP_MODE_SUSPEND;
	return sm5705_write(chg, SM5705_REG_CNTL, cntl);
}

static void sm5705_work(struct work_struct *work)
{
	struct sm5705_charger *chg =
		container_of(work, struct sm5705_charger, work.work);
	bool was_vbus = chg->vbus;
	bool was_charging = chg->charging;

	sm5705_update_state(chg);

	/* The chip's watchdog clears CHGON on its own after a while; re-assert
	 * the charge-on mode and reload the timer on every poll while VBUS is
	 * present, otherwise the phone silently stops charging.
	 */
	if (chg->vbus && enable_charging) {
		sm5705_write(chg, SM5705_REG_CHGCNTL8, SM5705_CHGCNTL8_WDT_RST);
		if (!chg->charging)
			sm5705_write(chg, SM5705_REG_CNTL, SM5705_OP_MODE_CHG_ON);
	}

	if (was_vbus != chg->vbus) {
		sm5705_configure(chg);
		sm5705_dump_regs(chg, chg->vbus ? "VBUS on" : "VBUS off");
		power_supply_changed(chg->psy);
	} else if (was_charging != chg->charging) {
		power_supply_changed(chg->psy);
	}

	schedule_delayed_work(&chg->work, msecs_to_jiffies(SM5705_POLL_MS));
}

static irqreturn_t sm5705_irq(int irq, void *data)
{
	struct sm5705_charger *chg = data;
	u8 int1 = 0, int2 = 0;

	sm5705_read(chg, SM5705_REG_INT1, &int1);
	sm5705_read(chg, SM5705_REG_INT2, &int2);
	if (int1 || int2)
		dev_dbg(chg->dev, "irq int1=%02x int2=%02x\n", int1, int2);

	schedule_delayed_work(&chg->work, msecs_to_jiffies(200));

	return IRQ_HANDLED;
}

static int sm5705_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct sm5705_charger *chg = power_supply_get_drvdata(psy);
	u8 st2 = 0;

	sm5705_update_state(chg);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chg->vbus;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = chg->vbus;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (!chg->vbus) {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			break;
		}
		sm5705_read(chg, SM5705_REG_STATUS2, &st2);
		if (st2 & (SM5705_STATUS2_DONE | SM5705_STATUS2_TOPOFF))
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (chg->charging)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = chg->charging ? POWER_SUPPLY_CHARGE_TYPE_FAST :
					      POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property sm5705_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
};

static const struct power_supply_desc sm5705_psy_desc = {
	.name		= "sm5705-charger",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= sm5705_props,
	.num_properties	= ARRAY_SIZE(sm5705_props),
	.get_property	= sm5705_get_property,
};

static const struct regmap_config sm5705_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x3f,
};

static int sm5705_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {};
	struct sm5705_charger *chg;
	u8 devid = 0;
	int ret;

	chg = devm_kzalloc(&client->dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	chg->dev = &client->dev;
	chg->irq = client->irq;

	chg->regmap = devm_regmap_init_i2c(client, &sm5705_regmap_config);
	if (IS_ERR(chg->regmap))
		return dev_err_probe(chg->dev, PTR_ERR(chg->regmap),
				     "regmap init failed\n");

	ret = sm5705_read(chg, SM5705_REG_DEVICEID, &devid);
	if (ret)
		return dev_err_probe(chg->dev, ret, "cannot read DEVICEID\n");

	dev_info(chg->dev, "SM5705 charger, DEVICEID 0x%02x\n", devid);

	INIT_DELAYED_WORK(&chg->work, sm5705_work);

	sm5705_update_state(chg);
	ret = sm5705_configure(chg);
	if (ret)
		dev_warn(chg->dev, "initial configuration failed: %d\n", ret);
	sm5705_dump_regs(chg, "probe");

	psy_cfg.drv_data = chg;
	psy_cfg.fwnode = dev_fwnode(&client->dev);
	chg->psy = devm_power_supply_register(chg->dev, &sm5705_psy_desc,
					      &psy_cfg);
	if (IS_ERR(chg->psy))
		return dev_err_probe(chg->dev, PTR_ERR(chg->psy),
				     "cannot register power supply\n");

	if (chg->irq > 0) {
		ret = devm_request_threaded_irq(chg->dev, chg->irq, NULL,
						sm5705_irq,
						IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
						"sm5705-charger", chg);
		if (ret)
			return dev_err_probe(chg->dev, ret,
					     "cannot request IRQ %d\n", chg->irq);
	}

	i2c_set_clientdata(client, chg);

	schedule_delayed_work(&chg->work, msecs_to_jiffies(SM5705_POLL_MS));

	return 0;
}

static const struct of_device_id sm5705_of_match[] = {
	{ .compatible = "siliconmitus,sm5705-charger" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sm5705_of_match);

static struct i2c_driver sm5705_driver = {
	.driver = {
		.name = "sm5705-charger",
		.of_match_table = sm5705_of_match,
	},
	.probe = sm5705_probe,
};
module_i2c_driver(sm5705_driver);

MODULE_DESCRIPTION("Silicon Mitus SM5705 charger");
MODULE_LICENSE("GPL");
