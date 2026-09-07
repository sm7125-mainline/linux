// SPDX-License-Identifier: GPL-2.0-only
/*
 * Renesas ISL98608 display bias regulator
 *
 * The chip supplies the positive (AVDD) and negative (AVEE) bias rails of an
 * LCD panel from a single inductor. Both rails share one enable pin, and their
 * output voltages are I2C register values that reset to the part's power-on
 * defaults every time that pin is deasserted. A panel therefore has to have
 * them programmed before it is driven, not merely have the rails switched on.
 *
 * Only one register setting is known for certain: the vendor driver shipped for
 * the Samsung Galaxy Tab S6 Lite (2022) writes 0x08 to VBST, VP and VN on every
 * panel power-on and documents that as +/-5.5 V. The step size and offset of the
 * voltage encoding are not known, so this driver deliberately exposes the rails
 * as fixed 5.5 V rather than guessing a linear range; feeding a display bias
 * converter a miscalculated voltage is not a cheap mistake.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define ISL98608_REG_VBST		0x06
#define ISL98608_REG_VP			0x08
#define ISL98608_REG_VN			0x09
#define ISL98608_MAX_REGISTER		0x09

/* Vendor value for all three registers; VP/VN are +5.5 V and -5.5 V. */
#define ISL98608_VOLT_5V5_SEL		0x08
#define ISL98608_VOLT_5V5_UV		5500000

/* Time from asserting the enable pin until the chip answers on I2C. */
#define ISL98608_ENABLE_DELAY_US	2000

#define ISL98608_ID_VP			0
#define ISL98608_ID_VN			1
#define ISL98608_NUM_REGULATORS		2

struct isl98608 {
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	/* Guards enable_count, which is shared by both rails. */
	struct mutex lock;
	unsigned int enable_count;
};

static int isl98608_program(struct isl98608 *chip)
{
	int ret;

	ret = regmap_write(chip->regmap, ISL98608_REG_VBST,
			   ISL98608_VOLT_5V5_SEL);
	if (ret)
		return ret;

	ret = regmap_write(chip->regmap, ISL98608_REG_VP,
			   ISL98608_VOLT_5V5_SEL);
	if (ret)
		return ret;

	return regmap_write(chip->regmap, ISL98608_REG_VN,
			    ISL98608_VOLT_5V5_SEL);
}

/*
 * Both rails come up and down together, so the enable pin is reference counted
 * across the two regulators. The registers are rewritten every time the chip is
 * powered up, because it comes back with its defaults.
 */
static int isl98608_enable(struct regulator_dev *rdev)
{
	struct isl98608 *chip = rdev_get_drvdata(rdev);
	int ret = 0;

	mutex_lock(&chip->lock);

	if (!chip->enable_count) {
		gpiod_set_value_cansleep(chip->enable_gpio, 1);
		usleep_range(ISL98608_ENABLE_DELAY_US,
			     ISL98608_ENABLE_DELAY_US + 500);

		ret = isl98608_program(chip);
		if (ret) {
			dev_err(rdev_get_dev(rdev),
				"failed to program bias voltages: %d\n", ret);
			gpiod_set_value_cansleep(chip->enable_gpio, 0);
			goto out;
		}
	}

	chip->enable_count++;
out:
	mutex_unlock(&chip->lock);

	return ret;
}

static int isl98608_disable(struct regulator_dev *rdev)
{
	struct isl98608 *chip = rdev_get_drvdata(rdev);

	mutex_lock(&chip->lock);

	if (chip->enable_count && !--chip->enable_count)
		gpiod_set_value_cansleep(chip->enable_gpio, 0);

	mutex_unlock(&chip->lock);

	return 0;
}

static int isl98608_is_enabled(struct regulator_dev *rdev)
{
	struct isl98608 *chip = rdev_get_drvdata(rdev);
	int enabled;

	mutex_lock(&chip->lock);
	enabled = chip->enable_count > 0;
	mutex_unlock(&chip->lock);

	return enabled;
}

static const struct regulator_ops isl98608_ops = {
	.enable		= isl98608_enable,
	.disable	= isl98608_disable,
	.is_enabled	= isl98608_is_enabled,
};

/*
 * VN is a negative rail; like other display bias drivers this reports its
 * magnitude, because consumers routinely treat a negative regulator_get_voltage()
 * return as an error code.
 */
#define ISL98608_REGULATOR_DESC(_id, _name)			\
	[ISL98608_ID_##_id] = {					\
		.name = "isl98608-"#_name,			\
		.supply_name = "vin",				\
		.id = ISL98608_ID_##_id,			\
		.of_match = of_match_ptr(#_name),		\
		.regulators_node = of_match_ptr("regulators"),	\
		.ops = &isl98608_ops,				\
		.n_voltages = 1,				\
		.fixed_uV = ISL98608_VOLT_5V5_UV,		\
		.enable_time = ISL98608_ENABLE_DELAY_US,	\
		.type = REGULATOR_VOLTAGE,			\
		.owner = THIS_MODULE,				\
	}

static const struct regulator_desc isl98608_regs_desc[ISL98608_NUM_REGULATORS] = {
	ISL98608_REGULATOR_DESC(VP, vp),
	ISL98608_REGULATOR_DESC(VN, vn),
};

static const struct regmap_config isl98608_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= ISL98608_MAX_REGISTER,
	.cache_type	= REGCACHE_NONE,
};

static int isl98608_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct isl98608 *chip;
	int id;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->regmap = devm_regmap_init_i2c(client, &isl98608_regmap_config);
	if (IS_ERR(chip->regmap))
		return dev_err_probe(dev, PTR_ERR(chip->regmap),
				     "regmap init failed\n");

	/*
	 * The bootloader leaves the chip enabled to keep its splash on screen.
	 * Request the pin as-is so handover does not blink the rails, and let
	 * the first enable() program the registers.
	 */
	chip->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(chip->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(chip->enable_gpio),
				     "failed to get enable GPIO\n");

	mutex_init(&chip->lock);
	i2c_set_clientdata(client, chip);

	for (id = 0; id < ISL98608_NUM_REGULATORS; id++) {
		config.dev = dev;
		config.regmap = chip->regmap;
		config.driver_data = chip;

		rdev = devm_regulator_register(dev, &isl98608_regs_desc[id],
					       &config);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev),
					     "failed to register %s\n",
					     isl98608_regs_desc[id].name);
	}

	return 0;
}

static const struct i2c_device_id isl98608_id[] = {
	{ "isl98608" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isl98608_id);

static const struct of_device_id isl98608_of_match[] = {
	{ .compatible = "isil,isl98608" },
	{ }
};
MODULE_DEVICE_TABLE(of, isl98608_of_match);

static struct i2c_driver isl98608_i2c_driver = {
	.driver = {
		.name = "isl98608",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = isl98608_of_match,
	},
	.probe = isl98608_probe,
	.id_table = isl98608_id,
};

module_i2c_driver(isl98608_i2c_driver);

MODULE_DESCRIPTION("Renesas ISL98608 display bias regulator driver");
MODULE_LICENSE("GPL");
