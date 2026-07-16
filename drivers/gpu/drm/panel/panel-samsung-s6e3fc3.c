// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 map220v <map220v300@gmail.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct panel_info {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct panel_desc *desc;
	enum drm_panel_orientation orientation;

	struct gpio_desc *reset_gpio;
	struct backlight_device *backlight;
	struct regulator_bulk_data supplies[2];
};

struct panel_desc {
	unsigned int width_mm;
	unsigned int height_mm;

	unsigned int bpc;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;

	const struct drm_display_mode *modes;
	unsigned int num_modes;
	const struct mipi_dsi_device_info dsi_info;
	int (*init_sequence)(struct panel_info *pinfo);
};

static inline struct panel_info *to_panel_info(struct drm_panel *panel)
{
	return container_of(panel, struct panel_info, panel);
}

static int ams667ym01_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_device *dsi = pinfo->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(30);

	/* Unlock Level 1 Key */
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
	/* Unlock Level 0 Key */
	//mipi_dsi_dcs_write_seq(dsi, 0x9f, 0xa5, 0xa5);

	mipi_dsi_dcs_write_seq(dsi, 0xf2,
					0x00, 0x05, 0x0e, 0x58, 0x54, 0x00, 0x0c, 0x00,
					0x04, 0x30, 0xb8, 0x30, 0xb8, 0x0c, 0x04, 0xbc,
					0x26, 0xe8, 0x0c, 0x00, 0x04, 0x10, 0x00, 0x10,
					0x26, 0xa8, 0x10, 0x00, 0x10, 0x10, 0x34, 0x10,
					0x00, 0x40, 0x30, 0xc8, 0x00, 0xc8, 0x00, 0x00,
					0xce);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	/* Set Column Address to 1079 */
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37);
	/* Set Page Address to 2399 */
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x5f);

	/* Partial update limitation */
	mipi_dsi_dcs_write_seq(dsi, 0xc2,
					0x1b, 0x41, 0xb0, 0x0e, 0x00, 0x3c, 0x5a, 0x00,
					0x00);

	/* Unlock Level 2 Key */
	mipi_dsi_dcs_write_seq(dsi, 0xfc, 0x5a, 0x5a);

	/* Default 1711 Mbps (REV D) */
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x2a, 0xc5);
	mipi_dsi_dcs_write_seq(dsi, 0xc5, 0x0d, 0x10, 0x80, 0x45);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x3e, 0xc5);
	mipi_dsi_dcs_write_seq(dsi, 0xc5, 0x32, 0xb1);

	/* Lock Level 2 Key */
	mipi_dsi_dcs_write_seq(dsi, 0xfc, 0xa5, 0xa5);

	/* ERR_FG Setting */
	mipi_dsi_dcs_write_seq(dsi, 0xe5, 0x15);
	mipi_dsi_dcs_write_seq(dsi, 0xed, 0x44, 0x4c, 0x20);

	/* PCD Setting */
	mipi_dsi_dcs_write_seq(dsi, 0xcc, 0x5c, 0x51);

	/* Frequency setting */
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x27, 0xf2);
	mipi_dsi_dcs_write_seq(dsi, 0xf2, 0x00);

	/* 90 FPS */
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0x08, 0x00);

	/* Smooth Dimming 4 frame */
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x92, 0x63);
	mipi_dsi_dcs_write_seq(dsi, 0x63, 0x04);

	/* Gamma mode 2 Normal (REV A) */
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x02, 0x90);
	mipi_dsi_dcs_write_seq(dsi, 0x90, 0x1C);
	mipi_dsi_dcs_write_seq(dsi, 0x53, 0x20);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x03, 0xFF);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x14);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x76, 0x63);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x63, 0x00, 0x00, 0x00);

	/* Frequency update */
	mipi_dsi_dcs_write_seq(dsi, 0xf7, 0x0f);
	/* Lock Level 0 Key */
	//mipi_dsi_dcs_write_seq(dsi, 0x9f, 0x5a, 0x5a);
	/* Lock Level 1 Key */
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);
	msleep(90);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int ams646yd04_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_device *dsi = pinfo->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(30);

	/* Unlock Level 1 Key */
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);

	mipi_dsi_dcs_write_seq(dsi, 0xf2,
					0x00, 0x05, 0x0e, 0x58, 0x54, 0x01, 0x0c, 0x00,
					0xb4, 0x26, 0xe4, 0x2f, 0xb0, 0x0c, 0x09, 0x74,
					0x26, 0xe4, 0x0c, 0x00, 0x04, 0x10, 0x00, 0x10,
					0x26, 0xa8, 0x10, 0x00, 0x10, 0x10, 0x34, 0x10,
					0x00, 0x40, 0x30, 0xc8, 0x00, 0xc8, 0x00, 0x00,
					0xce);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	/* Set Column Address to 1079 */
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37);
	/* Set Page Address to 2399 */
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x5f);

	/* Partial update limitation */
	mipi_dsi_dcs_write_seq(dsi, 0xc2,
					0x1b, 0x41, 0xb0, 0x0e, 0x00, 0x3c, 0x5a, 0x00,
					0x00);

	/* Unlock Level 2 Key */
	mipi_dsi_dcs_write_seq(dsi, 0xfc, 0x5a, 0x5a);

	/* Default 1711 Mbps (REV D) */
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x2a, 0xc5);
	mipi_dsi_dcs_write_seq(dsi, 0xc5, 0x0d, 0x10, 0x80, 0x45);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x2e, 0xc5);
	mipi_dsi_dcs_write_seq(dsi, 0xc5, 0x36, 0x41);

	/* Lock Level 2 Key */
	mipi_dsi_dcs_write_seq(dsi, 0xfc, 0xa5, 0xa5);

	/* ERR_FG Setting */
	mipi_dsi_dcs_write_seq(dsi, 0xe5, 0x15);
	mipi_dsi_dcs_write_seq(dsi, 0xed, 0x44, 0x4c, 0x20);

	/* PCD Setting */
	mipi_dsi_dcs_write_seq(dsi, 0xcc, 0x5c, 0x51);

	/* Frequency setting */
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x27, 0xf2);
	mipi_dsi_dcs_write_seq(dsi, 0xf2, 0x00);

	/* 90 FPS */
	usleep_range(17000, 18000);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x01, 0xb4, 0x65);
	mipi_dsi_generic_write_seq(dsi, 0x65,
				   0x0c, 0x94, 0x0c, 0x5a, 0x0c, 0x30, 0x0b,
				   0xb8, 0x0b, 0x46, 0x0a, 0xf6, 0x09, 0xea,
				   0x09, 0x08, 0x08, 0x1a, 0x07, 0x12, 0x06,
				   0x2a, 0x05, 0x12, 0x03, 0x82, 0x02, 0x08,
				   0x02, 0x08, 0x00, 0x18);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x00, 0x2a, 0x6a);
	mipi_dsi_generic_write_seq(dsi, 0x6a, 0x07, 0x00, 0xc0);
	mipi_dsi_generic_write_seq(dsi, 0xf7, 0x0f);
	usleep_range(17000, 18000);
	/* Frequency setting 1 */
	mipi_dsi_generic_write_seq(dsi, 0x60, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x00, 0x08, 0xf2);
	/* Frequency setting 2 */
	mipi_dsi_generic_write_seq(dsi, 0xf2, 0x04);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x00, 0x2c, 0x6a);
	/* Frequency setting 3 */
	mipi_dsi_generic_write_seq(dsi, 0x6a, 0x80);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x00, 0x28, 0x68);
	/* Frequency setting 4 */
	mipi_dsi_generic_write_seq(dsi, 0x68, 0x22);

	/* Smooth Dimming 4 frame */
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x92, 0x63);
	mipi_dsi_dcs_write_seq(dsi, 0x63, 0x04);

	/* Gamma mode 2 Normal (REV A) */
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x02, 0x90);
	mipi_dsi_dcs_write_seq(dsi, 0x90, 0x1C);
	mipi_dsi_dcs_write_seq(dsi, 0x53, 0x20);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x03, 0xFF);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x14);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x76, 0x63);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x63, 0x00, 0x00, 0x00);

	/* Frequency update */
	mipi_dsi_dcs_write_seq(dsi, 0xf7, 0x0f);
	/* Lock Level 1 Key */
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);
	msleep(90);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct drm_display_mode ams667ym01_modes[] = {
	{	/* 90 Hz */
		.clock = (1080 + 80 + 84 + 88) * (2400 + 15 + 2 + 2) * 90 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 80,
		.hsync_end = 1080 + 80 + 84,
		.htotal = 1080 + 80 + 84 + 88,
		.vdisplay = 2400,
		.vsync_start = 2400 + 15,
		.vsync_end = 2400 + 15 + 2,
		.vtotal = 2400 + 15 + 2 + 2,
	},
	{	/* 60 Hz */
		.clock = (1080 + 80 + 84 + 88) * (2400 + 16 + 2 + 2) * 60 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 80,
		.hsync_end = 1080 + 80 + 84,
		.htotal = 1080 + 80 + 84 + 88,
		.vdisplay = 2400,
		.vsync_start = 2400 + 16,
		.vsync_end = 2400 + 16 + 2,
		.vtotal = 2400 + 16 + 2 + 2,
	},
};

static const struct panel_desc ams667ym01_desc = {
	.modes = ams667ym01_modes,
	.num_modes = ARRAY_SIZE(ams667ym01_modes),
	.dsi_info = {
		.type = "s6e3fc3-ams667ym01",
		.channel = 0,
		.node = NULL,
	},
	.width_mm = 70,
	.height_mm = 155,
	.bpc = 8,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = ams667ym01_init_sequence,
};

static const struct panel_desc ams646yd04_desc = {
	.modes = ams667ym01_modes,
	.num_modes = ARRAY_SIZE(ams667ym01_modes),
	.dsi_info = {
		.type = "s6e3fc3-ams646yd04",
		.channel = 0,
		.node = NULL,
	},
	.width_mm = 67,
	.height_mm = 150,
	.bpc = 8,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = ams646yd04_init_sequence,
};

static void s6e3fc3_reset(struct panel_info *pinfo)
{
	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	usleep_range(12000, 13000);
}

static int s6e3fc3_enable(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int ret;

	ret = pinfo->desc->init_sequence(pinfo);
	if (ret < 0) {
		dev_err(panel->dev, "failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(pinfo->supplies), pinfo->supplies);
		return ret;
	}

	return 0;
}

static int s6e3fc3_prepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(pinfo->supplies), pinfo->supplies);
	if (ret < 0) {
		dev_err(panel->dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	s6e3fc3_reset(pinfo);

	return 0;
}

static int s6e3fc3_disable(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(pinfo->dsi);
	if (ret < 0)
		dev_err(&pinfo->dsi->dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(pinfo->dsi);
	if (ret < 0)
		dev_err(&pinfo->dsi->dev, "failed to enter sleep mode: %d\n", ret);

	msleep(70);

	return 0;
}

static int s6e3fc3_unprepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);

	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(pinfo->supplies), pinfo->supplies);

	return 0;
}

static void s6e3fc3_remove(struct mipi_dsi_device *dsi)
{
	struct panel_info *pinfo = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(pinfo->dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&pinfo->panel);
}

static int s6e3fc3_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int i;

	for (i = 0; i < pinfo->desc->num_modes; i++) {
		const struct drm_display_mode *m = &pinfo->desc->modes[i];
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
			return -ENOMEM;
		}

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (i == 0)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = pinfo->desc->width_mm;
	connector->display_info.height_mm = pinfo->desc->height_mm;
	connector->display_info.bpc = pinfo->desc->bpc;

	return pinfo->desc->num_modes;
}

static enum drm_panel_orientation s6e3fc3_get_orientation(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);

	return pinfo->orientation;
}

static const struct drm_panel_funcs s6e3fc3_panel_funcs = {
	.disable = s6e3fc3_disable,
	.enable = s6e3fc3_enable,
	.prepare = s6e3fc3_prepare,
	.unprepare = s6e3fc3_unprepare,
	.get_modes = s6e3fc3_get_modes,
	.get_orientation = s6e3fc3_get_orientation,
};

static int s6e3fc3_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int s6e3fc3_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops s6e3fc3_bl_ops = {
	.update_status = s6e3fc3_bl_update_status,
	.get_brightness = s6e3fc3_bl_get_brightness,
};

static struct backlight_device *s6e3fc3_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1023,
		.max_brightness = 1023,
		.scale = BACKLIGHT_SCALE_NON_LINEAR,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
							&s6e3fc3_bl_ops, &props);
}

static int s6e3fc3_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct panel_info *pinfo;
	int ret;

	pinfo = devm_kzalloc(dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	pinfo->supplies[0].supply = "vddio";
	pinfo->supplies[1].supply = "vci";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(pinfo->supplies),
				      pinfo->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	pinfo->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(pinfo->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(pinfo->reset_gpio), "failed to get reset gpio\n");

	pinfo->desc = of_device_get_match_data(dev);
	if (!pinfo->desc)
		return -ENODEV;

	pinfo->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, pinfo);
	drm_panel_init(&pinfo->panel, dev, &s6e3fc3_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	ret = of_drm_get_panel_orientation(dev->of_node, &pinfo->orientation);
	if (ret < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	pinfo->panel.prepare_prev_first = true;

	pinfo->panel.backlight = s6e3fc3_create_backlight(dsi);
	if (IS_ERR(pinfo->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(pinfo->panel.backlight),
						"failed to create backlight\n");

	drm_panel_add(&pinfo->panel);

	pinfo->dsi->lanes = pinfo->desc->lanes;
	pinfo->dsi->format = pinfo->desc->format;
	pinfo->dsi->mode_flags = pinfo->desc->mode_flags;

	ret = mipi_dsi_attach(pinfo->dsi);
	if (ret < 0) {
		ret = dev_err_probe(dev, ret, "cannot attach to DSI host.\n");
		drm_panel_remove(&pinfo->panel);
		return ret;
	}

	return 0;
}

static const struct of_device_id s6e3fc3_of_match[] = {
	{
		.compatible = "samsung,s6e3fc3-ams667ym01",
		.data = &ams667ym01_desc,
	},
	{
		.compatible = "samsung,s6e3fc3-ams646yd04",
		.data = &ams646yd04_desc,
	},
	{},
};
MODULE_DEVICE_TABLE(of, s6e3fc3_of_match);

static struct mipi_dsi_driver s6e3fc3_driver = {
	.probe = s6e3fc3_probe,
	.remove = s6e3fc3_remove,
	.driver = {
		.name = "panel-samsung-s6e3fc3",
		.of_match_table = s6e3fc3_of_match,
	},
};
module_mipi_dsi_driver(s6e3fc3_driver);

MODULE_AUTHOR("map220v <map220v300@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Samsung S6E3FC3 based MIPI DSI panels");
MODULE_LICENSE("GPL v2");
