// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Texas Instruments DRV2624 haptics chip.
 *
 * Copyright (c) 2016  Texas Instruments Inc.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/spinlock_types.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/input.h>

#include "drv2624.h"

static bool drv2624_is_volatile_reg(struct device *dev, unsigned int reg);

static int drv2624_reg_read(struct drv2624_data *drv2624, unsigned char reg)
{
	unsigned int val;
	int ret;

	ret = regmap_read(drv2624->regmap, reg, &val);
	if (ret < 0) {
		dev_err(drv2624->dev,
			"%s reg=0x%x error %d\n", __func__, reg, ret);
		return ret;
	}

	dev_dbg(drv2624->dev, "%s, Reg[0x%x]=0x%x\n", __func__, reg, val);

	return val;
}

static int drv2624_reg_write(struct drv2624_data *drv2624,
			     unsigned char reg, unsigned char val)
{
	int ret;

	ret = regmap_write(drv2624->regmap, reg, val);
	if (ret < 0) {
		dev_err(drv2624->dev,
			"%s reg=0x%x, value=0%x error %d\n",
			__func__, reg, val, ret);
	} else {
		dev_dbg(drv2624->dev, "%s, Reg[0x%x]=0x%x\n",
			__func__, reg, val);
	}

	return ret;
}

static int drv2624_set_bits(struct drv2624_data *drv2624,
			    unsigned char reg, unsigned char mask,
			    unsigned char val)
{
	int ret;

	if (drv2624_is_volatile_reg(drv2624->dev, reg))
		ret = regmap_write_bits(drv2624->regmap, reg, mask, val);
	else
		ret = regmap_update_bits(drv2624->regmap, reg, mask, val);
	if (ret < 0) {
		dev_err(drv2624->dev,
			"%s reg=%x, mask=0x%x, value=0x%x error %d\n",
			__func__, reg, mask, val, ret);
	} else {
		dev_dbg(drv2624->dev, "%s, Reg[0x%x]:M=0x%x, V=0x%x\n",
			__func__, reg, mask, val);
	}

	return ret;
}

static void drv2624_enable_irq(struct drv2624_data *drv2624, bool rtp)
{
	unsigned char mask;

	if (drv2624->irq <= 0)
		return;

	mask = INT_ENABLE_CRITICAL;

	if (rtp)
		mask = INT_ENABLE_ALL;

	drv2624_reg_read(drv2624, DRV2624_REG_STATUS);
	drv2624_reg_write(drv2624, DRV2624_REG_INT_ENABLE, mask);

	enable_irq(drv2624->irq);
}

static void drv2624_disable_irq(struct drv2624_data *drv2624)
{
	if (drv2624->irq <= 0)
		return;

	disable_irq(drv2624->irq);
	drv2624_reg_write(drv2624,
			  DRV2624_REG_INT_ENABLE, INT_MASK_ALL);
}

static void drv2624_reset(struct drv2624_data *drv2624)
{
	int ret;

	gpio_set_value(drv2624->plat_data.gpio_nrst, 0);
	usleep_range(1000, 2000);
	gpio_set_value(drv2624->plat_data.gpio_nrst, 1);
	usleep_range(1000, 2000);

	regcache_mark_dirty(drv2624->regmap);
	ret = regcache_sync(drv2624->regmap);
	if (ret) {
		dev_err(drv2624->dev, "Failed to sync cache: %d\n", ret);
		gpio_direction_output(drv2624->plat_data.gpio_nrst, 0);
	}
}

static int drv2624_poll_go_bit_stop(struct drv2624_data *drv2624)
{
	int ret, value;
	int poll_ready = POLL_GO_BIT_RETRY; /* to finish auto-brake */

	do {
		ret = drv2624_reg_read(drv2624, DRV2624_REG_GO);
		if (ret >= 0) {
			value = ret & 0x01;
			if (!value)
				return ret;
		}
		usleep_range(8000, 10000);
	} while (poll_ready--);

	dev_err(drv2624->dev, "%s, ERROR: failed to clear GO\n", __func__);

	return -EINVAL;
}

static int drv2624_set_go_bit(struct drv2624_data *drv2624, unsigned char val)
{
	int ret;
	int retry = POLL_GO_BIT_RETRY;

	val &= 0x01;

	do {
		ret = drv2624_set_bits(drv2624, DRV2624_REG_GO, 0x01, val);
		if (ret >= 0) {
			usleep_range(1000, 1100);
			/* Only poll GO bit for STOP */
			if (!val)
				ret = drv2624_poll_go_bit_stop(drv2624);
			return ret;
		}
		usleep_range(8000, 10000);
	} while (retry--);

	drv2624_reset(drv2624);

	return ret;
}

static void drv2624_stop(struct drv2624_data *drv2624)
{
	if (drv2624->vibrator_playing) {
		dev_dbg(drv2624->dev, "%s\n", __func__);
		drv2624_disable_irq(drv2624);
		drv2624_set_go_bit(drv2624, STOP);
		drv2624->work_mode = WORK_IDLE;
		drv2624->vibrator_playing = false;
		pm_relax(drv2624->dev);
	}
}

static void drv2624_haptics_work(struct work_struct *work)
{
	struct drv2624_data *drv2624 =
		container_of(work, struct drv2624_data, work);
	int ret = 0;

	mutex_lock(&drv2624->lock);

	if (drv2624->rtp_input) {
		if (!drv2624->vibrator_playing) {
			pm_stay_awake(drv2624->dev);
			drv2624->vibrator_playing = true;
			drv2624_enable_irq(drv2624, true);

			ret = drv2624_set_go_bit(drv2624, GO);
			if (ret < 0) {
				dev_warn(drv2624->dev, "Start playback failed\n");
				drv2624->vibrator_playing = false;
				drv2624_disable_irq(drv2624);
				pm_relax(drv2624->dev);
			} else {
				drv2624->work_mode |= WORK_VIBRATOR;
			}
		}

		drv2624_reg_write(drv2624, DRV2624_REG_RTP_INPUT, drv2624->rtp_input);
	} else {
		drv2624_stop(drv2624);
	}

	mutex_unlock(&drv2624->lock);
}

static void vibrator_work_routine(struct work_struct *work)
{
	struct drv2624_data *drv2624 =
	    container_of(work, struct drv2624_data, vibrator_work);
	unsigned char mode = MODE_RTP;
	unsigned char status;
	int ret = 0;

	mutex_lock(&drv2624->lock);

	dev_dbg(drv2624->dev, "%s, afer mnWorkMode=0x%x\n",
		__func__, drv2624->work_mode);

	if (drv2624->work_mode & WORK_IRQ) {
		ret = drv2624_reg_read(drv2624, DRV2624_REG_STATUS);
		if (ret >= 0)
			drv2624->int_status = ret;

		drv2624_disable_irq(drv2624);

		if (ret < 0) {
			dev_err(drv2624->dev,
				"%s, reg read error\n", __func__);
			goto err;
		}

		status = drv2624->int_status;
		dev_dbg(drv2624->dev, "%s, status=0x%x\n",
			__func__, drv2624->int_status);

		if (status & OVERCURRENT_MASK) {
			dev_err(drv2624->dev,
				"ERROR, Over Current detected!!\n");
		}

		if (status & OVERTEMPRATURE_MASK) {
			dev_err(drv2624->dev,
				"ERROR, Over Temperature detected!!\n");
		}

		if (status & ULVO_MASK)
			dev_err(drv2624->dev, "ERROR, VDD drop observed!!\n");

		if (status & PRG_ERR_MASK)
			dev_err(drv2624->dev, "ERROR, PRG error!!\n");

		if (status & PROCESS_DONE_MASK) {
			ret = drv2624_reg_read(drv2624, DRV2624_REG_MODE);
			if (ret < 0) {
				dev_err(drv2624->dev,
					"%s, reg read error\n", __func__);
				goto err;
			}

			mode = ret & WORKMODE_MASK;
			if (mode == MODE_CALIBRATION) {
				drv2624->auto_cal_result.result = status;
				if ((status & DIAG_MASK) != DIAG_SUCCESS) {
					dev_err(drv2624->dev,
						"Calibration fail\n");
				} else {
					drv2624->auto_cal_result.cal_comp =
					    drv2624_reg_read(drv2624,
							     DRV2624_REG_CAL_COMP);
					drv2624->auto_cal_result.cal_bemf =
					    drv2624_reg_read(drv2624,
							     DRV2624_REG_CAL_BEMF);
					drv2624->auto_cal_result.cal_gain =
					    drv2624_reg_read(drv2624,
							     DRV2624_REG_CAL_COMP)
					    & BEMFGAIN_MASK;
					dev_dbg(drv2624->dev,
						"AutoCal : Comp=0x%x, Bemf=0x%x, Gain=0x%x\n",
						drv2624->auto_cal_result.cal_comp,
						drv2624->auto_cal_result.cal_bemf,
						drv2624->auto_cal_result.cal_gain);
				}
			} else if (mode == MODE_DIAGNOSTIC) {
				drv2624->diag_result.result = status;
				if ((status & DIAG_MASK) != DIAG_SUCCESS) {
					dev_err(drv2624->dev,
						"Diagnostic fail\n");
				} else {
					drv2624->diag_result.diagz =
					    drv2624_reg_read(drv2624,
							     DRV2624_REG_DIAG_Z);
					drv2624->diag_result.diagk =
					    drv2624_reg_read(drv2624,
							     DRV2624_REG_DIAG_K);
					dev_dbg(drv2624->dev,
						"Diag : ZResult=0x%x, CurrentK=0x%x\n",
						drv2624->diag_result.diagz,
						drv2624->diag_result.diagk);
				}
			} else if (mode == MODE_WAVEFORM_SEQUENCER) {
				dev_dbg(drv2624->dev,
					"Waveform Sequencer Playback finished\n");
			} else if (mode == MODE_RTP) {
				dev_dbg(drv2624->dev, "RTP IRQ\n");
			}
		}

		if ((mode != MODE_RTP) && drv2624->vibrator_playing) {
			dev_info(drv2624->dev, "release wklock\n");
			drv2624->vibrator_playing = false;
			pm_relax(drv2624->dev);
		}

		drv2624->work_mode &= ~WORK_IRQ;
	}

	if (drv2624->work_mode & WORK_VIBRATOR) {
		drv2624_stop(drv2624);
		drv2624->work_mode &= ~WORK_VIBRATOR;
	}
err:

	mutex_unlock(&drv2624->lock);
}

static int drv2624_haptics_play(struct input_dev *dev, void *data __always_unused,
				struct ff_effect *effect)
{
	struct drv2624_data *drv2624 = input_get_drvdata(dev);

	drv2624->rtp_input = ((int) effect->u.rumble.strong_magnitude
			    + (int) effect->u.rumble.weak_magnitude) >> 10;

	queue_work(drv2624->drv2624_wq, &drv2624->work);

	return 0;
}

static void drv2624_close(struct input_dev *dev)
{
	struct drv2624_data *drv2624 = input_get_drvdata(dev);

	cancel_work_sync(&drv2624->work);
}

static int haptics_init(struct drv2624_data *drv2624)
{
	int ret;

	drv2624->input_dev = devm_input_allocate_device(drv2624->dev);
	drv2624->input_dev->name = "drv2624:haptics";
	drv2624->input_dev->close = drv2624_close;
	input_set_drvdata(drv2624->input_dev, drv2624);
	input_set_capability(drv2624->input_dev, EV_FF, FF_RUMBLE);

	ret = input_ff_create_memless(drv2624->input_dev, drv2624, drv2624_haptics_play);
	if (ret) {
		dev_err(drv2624->dev, "Could not create input device: %d\n", ret);
		return ret;
	}

	ret = input_register_device(drv2624->input_dev);
	if (ret) {
		dev_err(drv2624->dev, "Could not register input device: %d\n", ret);
		return ret;
	}

	device_init_wakeup(drv2624->dev, false);
	mutex_init(&drv2624->lock);

	drv2624->drv2624_wq =
		alloc_ordered_workqueue("drv2624_wq", WQ_HIGHPRI);
	if (!drv2624->drv2624_wq) {
		dev_err(drv2624->dev,
			"drv2624: fail to alloc_workqueue for drv2624_wq\n");
		return -ENOMEM;
	}

	INIT_WORK(&drv2624->vibrator_work, vibrator_work_routine);
	INIT_WORK(&drv2624->work, drv2624_haptics_work);

	return 0;
}

static void dev_init_platform_data(struct drv2624_data *drv2624)
{
	struct drv2624_platform_data *pdata = &drv2624->plat_data;
	struct actuator_data actuator = pdata->actuator;
	unsigned char value_temp = 0;
	unsigned char mask_temp = 0;

	drv2624_set_bits(drv2624,
			 DRV2624_REG_MODE, PINFUNC_MASK,
			 (PINFUNC_INT << PINFUNC_SHIFT));

	if ((actuator.actuator_type == ERM) ||
	    (actuator.actuator_type == LRA)) {
		mask_temp |= ACTUATOR_MASK;
		value_temp |= (actuator.actuator_type << ACTUATOR_SHIFT);
	}

	if ((pdata->loop == CLOSE_LOOP) ||
	    (pdata->loop == OPEN_LOOP)) {
		mask_temp |= LOOP_MASK;
		value_temp |= (pdata->loop << LOOP_SHIFT);
	}

	if (value_temp != 0) {
		drv2624_set_bits(drv2624,
				 DRV2624_REG_CONTROL1,
				 mask_temp | AUTOBRK_OK_MASK,
				 value_temp | AUTOBRK_OK_ENABLE);
	}

	value_temp = 0;
	if (actuator.actuator_type == ERM)
		value_temp = LIB_ERM;
	else if (actuator.actuator_type == LRA)
		value_temp = LIB_LRA;
	if (value_temp != 0) {
		drv2624_set_bits(drv2624,
				 DRV2624_REG_CONTROL2, LIB_MASK,
				 value_temp << LIB_SHIFT);
	}

	if (actuator.rated_voltage != 0) {
		drv2624_reg_write(drv2624,
				  DRV2624_REG_RATED_VOLTAGE,
				  actuator.rated_voltage);
	} else {
		dev_err(drv2624->dev, "%s, ERROR Rated ZERO\n", __func__);
	}

	if (actuator.over_drive_clamp_voltage != 0) {
		drv2624_reg_write(drv2624,
				  DRV2624_REG_OVERDRIVE_CLAMP,
				  actuator.over_drive_clamp_voltage);
	} else {
		dev_err(drv2624->dev,
			"%s, ERROR OverDriveVol ZERO\n", __func__);
	}

	if (actuator.actuator_type == LRA) {
		unsigned char drive_time =
		    5 * (1000 - actuator.lra_freq) / actuator.lra_freq;
		unsigned short open_loop_period =
		    (unsigned short)((unsigned int)1000000000 /
				     (24615 * actuator.lra_freq));

		if (actuator.lra_freq < 125)
			drive_time |= (MINFREQ_SEL_45HZ << MINFREQ_SEL_SHIFT);

		drv2624_set_bits(drv2624,
				 DRV2624_REG_DRIVE_TIME,
				 DRIVE_TIME_MASK | MINFREQ_SEL_MASK, drive_time);

		if (actuator.ol_lra_freq > -1)
			open_loop_period =
				(unsigned short)((unsigned int)1000000000 /
				     (24615 * actuator.ol_lra_freq));

		drv2624_set_bits(drv2624,
				 DRV2624_REG_OL_PERIOD_H, 0x03,
				 (open_loop_period & 0x0300) >> 8);
		drv2624_reg_write(drv2624, DRV2624_REG_OL_PERIOD_L,
				  (open_loop_period & 0x00ff));

		dev_info(drv2624->dev,
			 "%s, LRA = %d, drive_time=0x%x\n",
			 __func__, actuator.lra_freq, drive_time);

		if (actuator.lra_wave_shape > -1)
			drv2624_set_bits(drv2624,
					 DRV2624_REG_LRA_OL_CTRL,
					 LRA_WAVE_SHAPE_MASK,
					 actuator.lra_wave_shape);
	}

	if (actuator.voltage_comp > -1)
		drv2624_reg_write(drv2624, DRV2624_REG_CAL_COMP,
				  actuator.voltage_comp);

	if (actuator.bemf_factor > -1)
		drv2624_reg_write(drv2624, DRV2624_REG_CAL_BEMF,
				  actuator.bemf_factor);

	if (actuator.bemf_gain > -1)
		drv2624_set_bits(drv2624, DRV2624_REG_LOOP_CONTROL,
				 BEMFGAIN_MASK, actuator.bemf_gain);

	if (actuator.fb_brake_factor > -1)
		drv2624_set_bits(drv2624, DRV2624_REG_LOOP_CONTROL,
				 FB_BRAKE_FACTOR_MASK,
				 actuator.fb_brake_factor);

	if (actuator.blanking_time > -1)
		drv2624_set_bits(drv2624, DRV2624_REG_BLK_IDISS_TIME,
				 BLANKING_TIME_MASK,
				 actuator.blanking_time << BLANKING_TIME_SHIFT);

	if (actuator.idiss_time > -1)
		drv2624_set_bits(drv2624, DRV2624_REG_BLK_IDISS_TIME,
				 IDISS_TIME_MASK, actuator.idiss_time);

	if (actuator.zc_det_time > -1)
		drv2624_set_bits(drv2624, DRV2624_REG_ZC_OD_TIME,
				 ZC_DET_TIME_MASK, actuator.zc_det_time);
}

static irqreturn_t drv2624_irq_handler(int irq, void *dev_id)
{
	struct drv2624_data *drv2624 = (struct drv2624_data *)dev_id;

	drv2624->work_mode |= WORK_IRQ;

	schedule_work(&drv2624->vibrator_work);

	return IRQ_HANDLED;
}

static int drv2624_parse_dt(struct device *dev, struct drv2624_data *drv2624)
{
	struct device_node *np = dev->of_node;
	struct drv2624_platform_data *pdata = &drv2624->plat_data;
	int ret = 0;
	unsigned int value;

	pdata->gpio_nrst = of_get_named_gpio(np, "reset-gpios", 0);
	if (!gpio_is_valid(pdata->gpio_nrst)) {
		dev_err(drv2624->dev,
			"Looking up %s property in node %s failed %d\n",
			"ti,reset-gpio", np->full_name, pdata->gpio_nrst);
		ret = -EINVAL;
		goto drv2624_parse_dt_out;
	}

	ret = of_property_read_u32(np, "ti,smart-loop", &value);
	if (ret) {
		dev_err(drv2624->dev,
			"Looking up %s property in node %s failed %d\n",
			"ti,smart-loop", np->full_name, ret);
			ret = -EINVAL;
			goto drv2624_parse_dt_out;
	}

	pdata->loop = value & 0x01;
	dev_dbg(drv2624->dev, "ti,smart-loop=%d\n", pdata->loop);

	ret = of_property_read_u32(np, "ti,actuator", &value);
	if (ret) {
		dev_err(drv2624->dev,
			"Looking up %s property in node %s failed %d\n",
			"ti,actuator", np->full_name, ret);
		ret = -EINVAL;
		goto drv2624_parse_dt_out;
	}

	pdata->actuator.actuator_type = value & 0x01;
	dev_dbg(drv2624->dev, "ti,actuator=%d\n",
		pdata->actuator.actuator_type);

	ret = of_property_read_u32(np, "ti,rated-voltage", &value);
	if (ret) {
		dev_err(drv2624->dev,
			"Looking up %s property in node %s failed %d\n",
			"ti,rated-voltage", np->full_name, ret);
		ret = -EINVAL;
		goto drv2624_parse_dt_out;
	}

	pdata->actuator.rated_voltage = value;
	dev_dbg(drv2624->dev, "ti,rated-voltage=0x%x\n",
		pdata->actuator.rated_voltage);

	ret = of_property_read_u32(np, "ti,odclamp-voltage", &value);
	if (ret) {
		dev_err(drv2624->dev,
			"Looking up %s property in node %s failed %d\n",
			"ti,odclamp-voltage", np->full_name, ret);
		ret = -EINVAL;
		goto drv2624_parse_dt_out;
	}

	pdata->actuator.over_drive_clamp_voltage = value;
	dev_dbg(drv2624->dev, "ti,odclamp-voltage=0x%x\n",
		pdata->actuator.over_drive_clamp_voltage);

	if (pdata->actuator.actuator_type == LRA) {
		ret = of_property_read_u32(np, "ti,lra-frequency", &value);
		if (ret) {
			dev_err(drv2624->dev,
				"Looking up %s property in node %s failed %d\n",
				"ti,lra-frequency", np->full_name, ret);
			ret = -EINVAL;
			goto drv2624_parse_dt_out;
		} else {
			if ((value >= 45) && (value <= 300)) {
				pdata->actuator.lra_freq = value;
				dev_dbg(drv2624->dev,
					"ti,lra-frequency=%d\n",
					pdata->actuator.lra_freq);
			} else {
				dev_err(drv2624->dev,
					"ERROR, ti,lra-frequency=%d, out of range\n",
					value);
				ret = -EINVAL;
				goto drv2624_parse_dt_out;
			}
		}
	}

	/* actuator properties below are optional to have */

	if (!of_property_read_u32(np, "ti,voltage-comp", &value))
		pdata->actuator.voltage_comp = value;
	else
		pdata->actuator.voltage_comp = -1;

	dev_dbg(drv2624->dev, "ti,voltage-comp=%d\n",
		pdata->actuator.voltage_comp);

	if (!of_property_read_u32(np, "ti,ol-lra-frequency", &value)) {
		if ((value >= 45) && (value <= 300)) {
			pdata->actuator.ol_lra_freq = value;
		} else {
			pdata->actuator.ol_lra_freq = -1;
			dev_err(drv2624->dev,
				"ERROR, ti,ol-lra-frequency=%d, out of range\n",
				value);
		}
	} else {
		pdata->actuator.ol_lra_freq = -1;
	}

	dev_dbg(drv2624->dev, "ti,ol-lra-frequency=%d\n",
		pdata->actuator.ol_lra_freq);

	if (!of_property_read_u32(np, "ti,fb-brake-factor", &value))
		pdata->actuator.fb_brake_factor = value;
	else
		pdata->actuator.fb_brake_factor = -1;

	dev_dbg(drv2624->dev, "ti,fb-brake-factor=%d\n",
		pdata->actuator.fb_brake_factor);

	if (!of_property_read_u32(np, "ti,bemf-factor", &value))
		pdata->actuator.bemf_factor = value;
	else
		pdata->actuator.bemf_factor = -1;

	dev_dbg(drv2624->dev, "ti,bemf-factor=%d\n",
		pdata->actuator.bemf_factor);

	if (!of_property_read_u32(np, "ti,bemf-gain", &value))
		pdata->actuator.bemf_gain = value;
	else
		pdata->actuator.bemf_gain = -1;

	dev_dbg(drv2624->dev, "ti,bemf-gain=%d\n",
		pdata->actuator.bemf_gain);

	if (!of_property_read_u32(np, "ti,blanking-time", &value))
		pdata->actuator.blanking_time = value;
	else
		pdata->actuator.blanking_time = -1;

	dev_dbg(drv2624->dev, "ti,blanking-time=%d\n",
		pdata->actuator.ol_lra_freq);

	if (!of_property_read_u32(np, "ti,idiss-time", &value))
		pdata->actuator.idiss_time = value;
	else
		pdata->actuator.idiss_time = -1;

	dev_dbg(drv2624->dev, "ti,idiss-time=%d\n",
		pdata->actuator.idiss_time);

	if (!of_property_read_u32(np, "ti,zc-det-time", &value))
		pdata->actuator.zc_det_time = value;
	else
		pdata->actuator.zc_det_time = -1;

	dev_dbg(drv2624->dev, "ti,zc-det-time=%d\n",
		pdata->actuator.zc_det_time);

	if (!of_property_read_u32(np, "ti,lra-wave-shape", &value))
		pdata->actuator.lra_wave_shape = value;
	else
		pdata->actuator.lra_wave_shape = -1;

	dev_dbg(drv2624->dev, "ti,lra-wave-shape=%d\n",
		pdata->actuator.lra_wave_shape);

	if (!of_property_read_u32(np, "ti,waveform-interval", &value))
		pdata->actuator.waveform_interval = value;
	else
		pdata->actuator.waveform_interval = -1;

	dev_dbg(drv2624->dev, "ti,waveform-interval=%d\n",
		pdata->actuator.waveform_interval);

drv2624_parse_dt_out:
	return ret;
}

static const struct reg_default drv2624_reg_defaults[] = {
	{ DRV2624_REG_ID, 0x03 },
	{ DRV2624_REG_STATUS, 0x00 },
	{ DRV2624_REG_INT_ENABLE, INT_MASK_ALL },
	{ DRV2624_REG_DIAG_Z, 0x00 },
	{ DRV2624_REG_MODE, 0x44 },
	{ DRV2624_REG_LRA_PERIOD_H, 0x00 },
	{ DRV2624_REG_LRA_PERIOD_L, 0x00 },
	{ DRV2624_REG_CONTROL1, 0x88 },
	{ DRV2624_REG_GO, 0x00 },
	{ DRV2624_REG_CONTROL2, 0x00 },
	{ DRV2624_REG_RTP_INPUT, 0x7F },
	{ DRV2624_REG_SEQUENCER_1, 0x01 },
	{ DRV2624_REG_SEQ_LOOP_1, 0x00 },
	{ DRV2624_REG_SEQ_LOOP_2, 0x00 },
	{ DRV2624_REG_MAIN_LOOP, 0x00 },
	{ DRV2624_REG_RATED_VOLTAGE, 0x3F },
	{ DRV2624_REG_OVERDRIVE_CLAMP, 0x89 },
	{ DRV2624_REG_CAL_COMP, 0x0D },
	{ DRV2624_REG_CAL_BEMF, 0x6D },
	{ DRV2624_REG_LOOP_CONTROL, 0x36 },
	{ DRV2624_REG_DRIVE_TIME, 0x10 },
	{ DRV2624_REG_BLK_IDISS_TIME, 0x11 },
	{ DRV2624_REG_ZC_OD_TIME, 0x0C },
	{ DRV2624_REG_LRA_OL_CTRL, 0x00 },
	{ DRV2624_REG_OL_PERIOD_H, 0x00 },
	{ DRV2624_REG_OL_PERIOD_L, 0xC6 },
	{ DRV2624_REG_DIAG_K, 0x55 },
	{ DRV2624_REG_RAM_ADDR_UPPER, 0x00 },
	{ DRV2624_REG_RAM_ADDR_LOWER, 0x00 },
	{ DRV2624_REG_RAM_DATA, 0x32 },
};

static bool drv2624_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DRV2624_REG_GO:
	case DRV2624_REG_LRA_PERIOD_H:
	case DRV2624_REG_LRA_PERIOD_L:
	case DRV2624_REG_LOOP_CONTROL:
	case DRV2624_REG_DIAG_Z:
	case DRV2624_REG_DIAG_K:
	case DRV2624_REG_CAL_COMP:
	case DRV2624_REG_CAL_BEMF:
	case DRV2624_REG_RAM_ADDR_UPPER:
	case DRV2624_REG_RAM_ADDR_LOWER:
	case DRV2624_REG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool drv2624_is_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DRV2624_REG_STATUS:
		return true;
	default:
		return false;
	}
}

static bool drv2624_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DRV2624_REG_ID:
	case DRV2624_REG_STATUS:
	case DRV2624_REG_DIAG_Z:
	case DRV2624_REG_DIAG_K:
	case DRV2624_REG_LRA_PERIOD_H:
	case DRV2624_REG_LRA_PERIOD_L:
		return false;
	default:
		return true;
	}
}

static struct regmap_config drv2624_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_defaults = drv2624_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(drv2624_reg_defaults),
	.volatile_reg = drv2624_is_volatile_reg,
	.precious_reg = drv2624_is_precious_reg,
	.writeable_reg = drv2624_is_writeable_reg,
	.max_register = DRV2624_REG_RAM_DATA,
	.cache_type = REGCACHE_RBTREE,
	.can_multi_write = true,
};

static int drv2624_i2c_probe(struct i2c_client *client)
{
	struct drv2624_data *drv2624;
	int err = 0;

	drv2624 = kzalloc(sizeof(struct drv2624_data), GFP_KERNEL);
	if (!drv2624) {
		dev_err(&client->dev, "Could not allocate memory\n");
		return -ENOMEM;
	}

	drv2624->dev = &client->dev;
	i2c_set_clientdata(client, drv2624);
	dev_set_drvdata(&client->dev, drv2624);

	drv2624->regmap = devm_regmap_init_i2c(client, &drv2624_i2c_regmap);
	if (IS_ERR(drv2624->regmap)) {
		dev_err(drv2624->dev, "Failed to allocate register map: %ld\n",
				      PTR_ERR(drv2624->regmap));
		return PTR_ERR(drv2624->regmap);
	}

	if (client->dev.of_node) {
		dev_dbg(drv2624->dev, "of node parse\n");
		err = drv2624_parse_dt(&client->dev, drv2624);
		if (err) {
			dev_err(drv2624->dev,
				"%s: platform data error\n", __func__);
			return -ENODEV;
		}
	} else if (client->dev.platform_data) {
		dev_dbg(drv2624->dev, "platform data parse\n");
		memcpy(&drv2624->plat_data,
		       client->dev.platform_data,
		       sizeof(drv2624->plat_data));
	} else {
		dev_err(drv2624->dev, "%s: ERROR no platform data\n", __func__);
		return -ENODEV;
	}

	if (gpio_is_valid(drv2624->plat_data.gpio_nrst)) {
		err = devm_gpio_request(&client->dev,
					drv2624->plat_data.gpio_nrst,
					"DRV2624-NRST");
		if (err < 0) {
			dev_err(drv2624->dev,
				"%s: GPIO %d request NRST error\n",
				__func__, drv2624->plat_data.gpio_nrst);
			return err;
		}

		gpio_direction_output(drv2624->plat_data.gpio_nrst, 0);
		usleep_range(1000, 2000);
		gpio_direction_output(drv2624->plat_data.gpio_nrst, 1);
		usleep_range(1000, 2000); /* t(on) = 1ms */
	}

	err = drv2624_reg_read(drv2624, DRV2624_REG_ID);
	if (err < 0) {
		dev_err(drv2624->dev, "%s, i2c bus fail (%d)\n", __func__, err);
		goto drv2624_i2c_probe_err;
	} else {
		dev_info(drv2624->dev, "%s, ID status (0x%x)\n", __func__, err);
		drv2624->device_id = err;
	}

	if ((drv2624->device_id & 0xf0) != DRV2624_ID) {
		dev_err(drv2624->dev, "%s, device_id(0x%x) fail\n",
			__func__, drv2624->device_id);
		goto drv2624_i2c_probe_err;
	}

	dev_init_platform_data(drv2624);

	drv2624->irq = of_irq_get(client->dev.of_node, 0);
	if (drv2624->irq > 0) {
		err = devm_request_threaded_irq(&client->dev, drv2624->irq,
						drv2624_irq_handler, NULL,
						IRQF_ONESHOT,
						client->name, drv2624);
		if (err < 0) {
			dev_err(drv2624->dev, "could not request interrupt: %d\n", err);
			goto drv2624_i2c_probe_err;
		}
	}

	err = haptics_init(drv2624);
	if (err)
		goto drv2624_i2c_probe_err;

	return 0;

drv2624_i2c_probe_err:
	dev_err(drv2624->dev, "%s failed, err=%d\n", __func__, err);
	return err;
}

static void drv2624_i2c_remove(struct i2c_client *client)
{
	struct drv2624_data *drv2624 = i2c_get_clientdata(client);

	cancel_work_sync(&drv2624->vibrator_work);
	cancel_work_sync(&drv2624->work);
	destroy_workqueue(drv2624->drv2624_wq);

	mutex_destroy(&drv2624->lock);
}

static const struct i2c_device_id drv2624_i2c_id[] = {
	{ "drv2624", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, drv2624_i2c_id);

static const struct of_device_id drv2624_of_match[] = {
	{ .compatible = "ti,drv2624" },
	{ },
};
MODULE_DEVICE_TABLE(of, drv2624_of_match);

static struct i2c_driver drv2624_i2c_driver = {
	.driver = {
		.name = "drv2624-haptics",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(drv2624_of_match),
	},
	.probe = drv2624_i2c_probe,
	.remove = drv2624_i2c_remove,
	.id_table = drv2624_i2c_id,
};
module_i2c_driver(drv2624_i2c_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_AUTHOR("Richard Acayan <mailingradian@gmail.com>");
MODULE_DESCRIPTION("DRV2624 I2C Smart Haptics driver");
MODULE_LICENSE("GPL");
