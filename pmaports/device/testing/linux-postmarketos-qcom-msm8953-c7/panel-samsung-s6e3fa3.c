// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Samsung S6E3FA3 AMS568HN31 panel (Galaxy C7 SM-C7000).
 *
 * Initialisation sequence converted from the downstream Samsung driver
 * drivers/video/msm/mdss/samsung/S6E3FA3_AMS568HN31.
 *
 * Power-up order follows the downstream mdss_dsi_panel.c code:
 *
 *   1. LCD_LDO_EN (enable-gpios, gpio94) high, the load switch that feeds
 *      the panel from pm8953_l17,
 *   2. reset pulse 20/20/20 ms (the downstream "reset-sequence = <1 20>,
 *      <0 20>, <1 20>"),
 *   3. DCS init sequence from dsi_panel_S6E3FA3_AMS568HN31_fhd_octa_cmd.dtsi,
 *   4. DCS display on (0x29), which the downstream display_on_tx_cmds_revA
 *      sends on the first frame update,
 *   5. DCS brightness (0x51) so that the panel is already visible before
 *      userspace restores the saved backlight level.
 *
 * The panel is a command mode (dsi_cmd_mode) panel: the DSI host pushes
 * complete frames into the panel's frame memory and the panel refreshes
 * itself, so no video-mode timing has to match.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define S6E3FA3_DEFAULT_BRIGHTNESS	300
#define S6E3FA3_MAX_BRIGHTNESS		365

struct s6e3fa3_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct regulator *vdd;
	struct regulator *vddio;
	struct backlight_device *bl;
};

static inline struct s6e3fa3_panel *to_s6e3fa3_panel(struct drm_panel *panel)
{
	return container_of(panel, struct s6e3fa3_panel, panel);
}

/*
 * Downstream reset-sequence = <1 20>, <0 20>, <1 20>, i.e. the line is
 * driven high, low, high again, each step 20 ms.  reset-gpios is
 * GPIO_ACTIVE_LOW, so value 0 drives the line high.
 */
static void s6e3fa3_panel_reset(struct s6e3fa3_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
}

static int s6e3fa3_panel_on(struct s6e3fa3_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	u16 brightness = S6E3FA3_DEFAULT_BRIGHTNESS;

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x1e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfd, 0x94);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x4c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xed, 0x44);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb6, 0x7f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb2, 0x00, 0xc2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb6, 0xbc, 0x0a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x7b, 0x1e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_msleep(&dsi_ctx, 80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3c, 0x00);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);

	if (ctx->bl)
		brightness = ctx->bl->props.brightness;
	mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);

	return dsi_ctx.accum_err;
}

static int s6e3fa3_panel_off(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 150);

	return dsi_ctx.accum_err;
}

static int s6e3fa3_panel_prepare(struct drm_panel *panel)
{
	struct s6e3fa3_panel *ctx = to_s6e3fa3_panel(panel);
	int ret;

	ret = regulator_enable(ctx->vddio);
	if (ret)
		return ret;
	ret = regulator_enable(ctx->vdd);
	if (ret)
		goto err_vdd;
	usleep_range(10000, 11000);

	/* LCD_LDO_EN: the load switch that connects vdd (pm8953_l17) to the
	 * panel. Without it the panel stays unpowered no matter what the DSI
	 * link does. */
	gpiod_set_value_cansleep(ctx->enable_gpio, 1);
	usleep_range(10000, 11000);

	s6e3fa3_panel_reset(ctx);

	ret = s6e3fa3_panel_on(ctx);
	if (ret < 0)
		goto err_reset;

	return 0;

err_reset:
	gpiod_set_value_cansleep(ctx->enable_gpio, 0);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->vdd);
err_vdd:
	regulator_disable(ctx->vddio);

	return ret;
}

static int s6e3fa3_panel_unprepare(struct drm_panel *panel)
{
	struct s6e3fa3_panel *ctx = to_s6e3fa3_panel(panel);

	s6e3fa3_panel_off(ctx->dsi);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	gpiod_set_value_cansleep(ctx->enable_gpio, 0);
	regulator_disable(ctx->vdd);
	regulator_disable(ctx->vddio);

	return 0;
}

static int s6e3fa3_panel_disable(struct drm_panel *panel)
{
	struct s6e3fa3_panel *ctx = to_s6e3fa3_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static const struct drm_display_mode s6e3fa3_panel_mode = {
	.clock = (1080 + 160 + 36 + 12) * (1920 + 7 + 7 + 2) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 160,
	.hsync_end = 1080 + 160 + 12,
	.htotal = 1080 + 160 + 36 + 12,
	.vdisplay = 1920,
	.vsync_start = 1920 + 7,
	.vsync_end = 1920 + 7 + 2,
	.vtotal = 1920 + 7 + 7 + 2,
	.width_mm = 69,
	.height_mm = 122,
};

static int s6e3fa3_panel_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &s6e3fa3_panel_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs s6e3fa3_panel_funcs = {
	.prepare = s6e3fa3_panel_prepare,
	.unprepare = s6e3fa3_panel_unprepare,
	.disable = s6e3fa3_panel_disable,
	.get_modes = s6e3fa3_panel_get_modes,
};

static int s6e3fa3_panel_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	/* The panel ignores brightness writes while it is in sleep mode, and
	 * userspace likes to restore the saved level while the display is
	 * still off. Re-send the value the next time we are in display on. */
	return 0;
}

static int s6e3fa3_panel_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	return brightness;
}

static const struct backlight_ops s6e3fa3_panel_bl_ops = {
	.update_status = s6e3fa3_panel_bl_update_status,
	.get_brightness = s6e3fa3_panel_bl_get_brightness,
};

static struct backlight_device *
s6e3fa3_panel_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = S6E3FA3_DEFAULT_BRIGHTNESS,
		.max_brightness = S6E3FA3_MAX_BRIGHTNESS,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6e3fa3_panel_bl_ops, &props);
}

static int s6e3fa3_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e3fa3_panel *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct s6e3fa3_panel, panel,
				   &s6e3fa3_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpio),
				     "Failed to get enable-gpios\n");

	ctx->vddio = devm_regulator_get(dev, "vddio");
	if (IS_ERR(ctx->vddio))
		return dev_err_probe(dev, PTR_ERR(ctx->vddio),
				     "Failed to get vddio supply\n");

	ctx->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ctx->vdd))
		return dev_err_probe(dev, PTR_ERR(ctx->vdd),
				     "Failed to get vdd supply\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	/* Command mode: no MIPI_DSI_MODE_VIDEO, the MDP5 command encoder
	 * pushes whole frames and the panel refreshes from its own GRAM. */
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = s6e3fa3_panel_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");
	ctx->bl = ctx->panel.backlight;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void s6e3fa3_panel_remove(struct mipi_dsi_device *dsi)
{
	struct s6e3fa3_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6e3fa3_panel_of_match[] = {
	{ .compatible = "samsung,s6e3fa3-ams568hn31" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e3fa3_panel_of_match);

static struct mipi_dsi_driver s6e3fa3_panel_driver = {
	.probe = s6e3fa3_panel_probe,
	.remove = s6e3fa3_panel_remove,
	.driver = {
		.name = "panel-samsung-s6e3fa3",
		.of_match_table = s6e3fa3_panel_of_match,
	},
};
module_mipi_dsi_driver(s6e3fa3_panel_driver);

MODULE_AUTHOR("nura-c7 contributors");
MODULE_DESCRIPTION("Samsung S6E3FA3 AMS568HN31 panel driver");
MODULE_LICENSE("GPL");
