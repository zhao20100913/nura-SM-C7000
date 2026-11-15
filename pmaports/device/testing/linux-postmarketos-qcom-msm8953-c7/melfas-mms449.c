// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Melfas MMS449 touchscreen controller (Galaxy C7 SM-C7000).
 *
 * The MMS449 is an autonomous controller: after power-on it scans the panel
 * and pushes 8 byte input records into an event FIFO. The host only has to
 * read the FIFO when the controller asserts its interrupt line. There is no
 * firmware download and no configuration write in the boot path, so the
 * driver is small.
 *
 * Protocol reverse engineered from the downstream driver
 * drivers/input/touchscreen/melfas/melfas_mms400.c and _mod.c:
 *
 *   read:  <page> <reg>  (write, no stop) then N bytes
 *   FIFO:  page 0x02 register 0x10, first byte = size | alert<<7,
 *          followed by size bytes of 8 byte records:
 *            [0] 0x80 press | id (0 based, stored +1)
 *            [1] low nibble  = x[11:8], high nibble = y[11:8]
 *            [2] x[7:0]   [3] y[7:0]
 *            [4] pressure [5] sum size
 *            [6] touch major   [7] touch minor
 *
 * Power: VDD_EN (enable-gpios) drives the panel supply switch; the
 * controller needs ~50 ms after it before it answers on I2C.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/module.h>
#include <linux/workqueue.h>

#define MMS449_MAX_FINGERS	10
#define MMS449_EVENT_SIZE	8
#define MMS449_RETRIES		3

/* Only used if the controller does not report its own resolution. */
#define MMS449_DEFAULT_WIDTH	1080
#define MMS449_DEFAULT_HEIGHT	1920

/* MIP register pages */
#define MMS_PAGE_INFO		0x01
#define MMS_PAGE_EVENT		0x02
#define MMS_PAGE_CTRL		0x06

/* page 0x01 (info) */
#define MMS_INFO_PRODUCT_NAME	0x00
#define MMS_INFO_RESOLUTION_X	0x10

/* page 0x02 (event) */
#define MMS_EVENT_PACKET_INFO	0x10

/* page 0x06 (control) */
#define MMS_CTRL_READY_STATUS	0x00

#define MMS_EVENT_PRESS		0x80
#define MMS_EVENT_ALERT		0x80	/* bit 7 of the size byte */
#define MMS_EVENT_ID		0x0f

struct mms449 {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *enable_gpio;
	struct touchscreen_properties props;
	unsigned int max_x;
	unsigned int max_y;
	u8 event_size;
	struct delayed_work poll_work;
};

static int mms449_i2c_read(struct mms449 *ts, u8 page, u8 reg, u8 *buf, int len)
{
	struct i2c_client *client = ts->client;
	u8 cmd[2] = { page, reg };
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = sizeof(cmd),
			.buf = cmd,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		},
	};
	int ret, i;

	for (i = 0; i < MMS449_RETRIES; i++) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret == ARRAY_SIZE(msgs))
			return 0;
	}

	return ret < 0 ? ret : -EIO;
}

static int mms449_i2c_read_cont(struct mms449 *ts, u8 *buf, int len)
{
	int ret, i;

	for (i = 0; i < MMS449_RETRIES; i++) {
		ret = i2c_master_recv(ts->client, buf, len);
		if (ret == len)
			return 0;
	}

	return ret < 0 ? ret : -EIO;
}

static void mms449_report(struct mms449 *ts, unsigned int size, const u8 *data)
{
	struct input_dev *input = ts->input;
	unsigned int i;

	for (i = 0; i + ts->event_size <= size; i += ts->event_size) {
		const u8 *rec = data + i;
		unsigned int id = (rec[0] & MMS_EVENT_ID) - 1;
		unsigned int x, y;

		if (id >= MMS449_MAX_FINGERS)
			continue;

		if (!(rec[0] & MMS_EVENT_PRESS)) {
			input_mt_slot(input, id);
			input_mt_report_slot_state(input, MT_TOOL_FINGER, false);
			continue;
		}

		x = rec[2] | ((rec[1] & 0x0f) << 8);
		y = rec[3] | (((rec[1] >> 4) & 0x0f) << 8);

		input_mt_slot(input, id);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);
		input_report_abs(input, ABS_MT_PRESSURE, rec[4]);
		input_report_abs(input, ABS_MT_TOUCH_MAJOR, rec[6]);
		input_report_abs(input, ABS_MT_TOUCH_MINOR, rec[7]);
	}

	input_mt_report_pointer_emulation(input, true);
	input_sync(input);
}

static void mms449_read_events(struct mms449 *ts)
{
	u8 buf[1 + MMS449_EVENT_SIZE * MMS449_MAX_FINGERS];
	unsigned int size, extra;
	int ret;

	ret = mms449_i2c_read(ts, MMS_PAGE_EVENT, MMS_EVENT_PACKET_INFO,
			      buf, 1 + ts->event_size);
	if (ret)
		return;

	/* Alert packets (ESD, wakeup gesture) carry no touch data. */
	if (buf[0] & MMS_EVENT_ALERT)
		return;

	size = buf[0] & 0x7f;
	if (size <= ts->event_size)
		goto report;

	extra = size - ts->event_size;
	if (extra > sizeof(buf) - 1 - ts->event_size)
		extra = sizeof(buf) - 1 - ts->event_size;

	ret = mms449_i2c_read_cont(ts, buf + 1 + ts->event_size, extra);
	if (ret)
		return;

report:
	mms449_report(ts, size, buf + 1);
}

static irqreturn_t mms449_irq(int irq, void *data)
{
	mms449_read_events(data);
	return IRQ_HANDLED;
}

/*
 * Fallback for boards where the interrupt line is not wired the way the
 * downstream device tree suggests: poll the event page instead. 10 ms is
 * fast enough to feel like normal touch input and costs ~100 tiny I2C
 * transfers per second.
 */
static int poll_ms = 10;
module_param(poll_ms, int, 0644);
MODULE_PARM_DESC(poll_ms, "poll the event FIFO every N ms (0 = interrupts only)");

static void mms449_poll(struct work_struct *work)
{
	struct mms449 *ts = container_of(to_delayed_work(work),
					 struct mms449, poll_work);

	mms449_read_events(ts);
	schedule_delayed_work(&ts->poll_work, msecs_to_jiffies(poll_ms));
}

static int mms449_power_on(struct mms449 *ts)
{
	gpiod_set_value_cansleep(ts->enable_gpio, 1);
	msleep(50);

	return 0;
}

static void mms449_power_off(struct mms449 *ts)
{
	gpiod_set_value_cansleep(ts->enable_gpio, 0);
	msleep(10);
}

static int mms449_read_info(struct mms449 *ts, u8 *info)
{
	int ret;

	ret = mms449_i2c_read(ts, MMS_PAGE_INFO, MMS_INFO_PRODUCT_NAME,
			      info, 16);
	if (ret)
		return ret;

	dev_info(&ts->client->dev, "MMS449: product \"%*pE\"\n", 16, info);

	ret = mms449_i2c_read(ts, MMS_PAGE_INFO, MMS_INFO_RESOLUTION_X,
			      info, 7);
	if (ret)
		return ret;

	ts->max_x = info[0] | (info[1] << 8);
	ts->max_y = info[2] | (info[3] << 8);
	if (!ts->max_x || !ts->max_y) {
		ts->max_x = MMS449_DEFAULT_WIDTH;
		ts->max_y = MMS449_DEFAULT_HEIGHT;
	}

	return 0;
}

static int mms449_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct mms449 *ts;
	struct input_dev *input;
	u8 info[16];
	int ret;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;

	ts->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						  GPIOD_OUT_LOW);
	if (IS_ERR(ts->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ts->enable_gpio),
				     "Failed to get enable-gpios\n");

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	ts->input = input;
	input->name = "Melfas MMS449 Touchscreen";
	input->id.bustype = BUS_I2C;
	input_set_drvdata(input, ts);

	ret = mms449_power_on(ts);
	if (ret)
		return ret;

	ret = mms449_read_info(ts, info);
	if (ret) {
		/* The controller may still be in whatever state the
		 * bootloader left it in; a power cycle makes it re-sync. */
		dev_warn(dev, "chip info unreadable (%d), retrying\n", ret);
		mms449_power_off(ts);
		usleep_range(10000, 11000);
		ret = mms449_power_on(ts);
		if (ret)
			return ret;
		ret = mms449_read_info(ts, info);
	}
	if (ret) {
		dev_err(dev, "Cannot read chip info: %d\n", ret);
		goto err_power;
	}

	ts->event_size = MMS449_EVENT_SIZE;

	dev_info(dev, "MMS449: %ux%u\n", ts->max_x, ts->max_y);

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, ts->max_x - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, ts->max_y - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);

	input_set_capability(input, EV_KEY, BTN_TOUCH);
	input_set_capability(input, EV_KEY, BTN_TOOL_FINGER);
	__set_bit(INPUT_PROP_DIRECT, input->propbit);

	touchscreen_parse_properties(input, true, &ts->props);

	ret = input_mt_init_slots(input, MMS449_MAX_FINGERS,
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (ret) {
		dev_err(dev, "Failed to init MT slots: %d\n", ret);
		goto err_power;
	}

	ret = devm_request_threaded_irq(dev, client->irq, NULL, mms449_irq,
					IRQF_ONESHOT, "melfas-mms449", ts);
	if (ret) {
		dev_err(dev, "Failed to request IRQ %d: %d\n", client->irq, ret);
		goto err_power;
	}

	if (poll_ms > 0) {
		INIT_DELAYED_WORK(&ts->poll_work, mms449_poll);
		schedule_delayed_work(&ts->poll_work, msecs_to_jiffies(poll_ms));
	}

	ret = input_register_device(input);
	if (ret) {
		dev_err(dev, "Failed to register input device: %d\n", ret);
		goto err_power;
	}

	i2c_set_clientdata(client, ts);

	return 0;

err_power:
	mms449_power_off(ts);

	return ret;
}

static void mms449_remove(struct i2c_client *client)
{
	struct mms449 *ts = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&ts->poll_work);

	mms449_power_off(ts);
}

static int mms449_suspend(struct device *dev)
{
	struct mms449 *ts = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&ts->poll_work);
	disable_irq(ts->client->irq);
	mms449_power_off(ts);

	return 0;
}

static int mms449_resume(struct device *dev)
{
	struct mms449 *ts = dev_get_drvdata(dev);

	mms449_power_on(ts);
	enable_irq(ts->client->irq);
	if (poll_ms > 0)
		schedule_delayed_work(&ts->poll_work, msecs_to_jiffies(poll_ms));

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(mms449_pm_ops, mms449_suspend, mms449_resume);

static const struct of_device_id mms449_of_match[] = {
	{ .compatible = "melfas,mms449" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mms449_of_match);

static const struct i2c_device_id mms449_id[] = {
	{ "mms449", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, mms449_id);

static struct i2c_driver mms449_driver = {
	.driver = {
		.name = "melfas-mms449",
		.of_match_table = mms449_of_match,
		.pm = pm_sleep_ptr(&mms449_pm_ops),
	},
	.probe = mms449_probe,
	.remove = mms449_remove,
	.id_table = mms449_id,
};
module_i2c_driver(mms449_driver);

MODULE_AUTHOR("nura-c7 contributors");
MODULE_DESCRIPTION("Melfas MMS449 touchscreen driver");
MODULE_LICENSE("GPL");
