// SPDX-License-Identifier: GPL-2.0
// STMicroelectronics FTS Touchscreen device driver
//
// Copyright (c) 2017 Samsung Electronics Co., Ltd.
// Copyright (c) 2017 Andi Shyti <andi@etezian.org>

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#define STMFTS_MAX_FINGERS	10
#define STMFTS_DEV_NAME		"stmfts"

/* FTS fts5cu56a */
struct fts_event_coordinate {
	u8 eid:2;
	u8 tid:4;
	u8 tchsta:2;
	u8 x_11_4;
	u8 y_11_4;
	u8 y_3_0:4;
	u8 x_3_0:4;
	u8 major;
	u8 minor;
	u8 z:6;
	u8 ttype_3_2:2;
	u8 left_event:5;
	u8 max_energy:1;
	u8 ttype_1_0:2;
	u8 noise_level;
	u8 max_strength;
	u8 hover_id_num:4;
	u8 reserved_10:4;
	u8 reserved_11;
	u8 reserved_12;
	u8 reserved_13;
	u8 reserved_14;
	u8 reserved_15;
} __packed;


/* 16 byte */
struct fts_event_status {
	u8 eid:2;
	u8 stype:4;
	u8 sf:2;
	u8 status_id;
	u8 status_data_1;
	u8 status_data_2;
	u8 status_data_3;
	u8 status_data_4;
	u8 status_data_5;
	u8 left_event_4_0:5;
	u8 max_energy:1;
	u8 reserved:2;
	u8 reserved_8;
	u8 reserved_9;
	u8 reserved_10;
	u8 reserved_11;
	u8 reserved_12;
	u8 reserved_13;
	u8 reserved_14;
	u8 reserved_15;
} __packed;

/* 16 byte */
struct fts_gesture_status {
	u8 eid:2;
	u8 stype:4;
	u8 sf:2;
	u8 gesture_id;
	u8 gesture_data_1;
	u8 gesture_data_2;
	u8 gesture_data_3;
	u8 gesture_data_4;
	u8 reserved_6;
	u8 left_event_4_0:5;
	u8 max_energy:1;
	u8 reserved_7:2;
	u8 reserved_8;
	u8 reserved_9;
	u8 reserved_10;
	u8 reserved_11;
	u8 reserved_12;
	u8 reserved_13;
	u8 reserved_14;
	u8 reserved_15;
} __packed;

/**
 * struct fts_finger - Represents fingers.
 * @ state: finger status (Event ID).
 * @ mcount: moving counter for debug.
 */
struct fts_finger {
	u8 id;
	u8 prev_ttype;
	u8 ttype;
	u8 action;
	u16 x;
	u16 y;
	u16 p_x;
	u16 p_y;
	u8 z;
	u8 hover_flag;
	u8 glove_flag;
	u8 touch_height;
	u16 mcount;
	u8 major;
	u8 minor;
	bool palm;
	int palm_count;
	u8 left_event;
	u8 max_energy;
	u16 max_energy_x;
	u16 max_energy_y;
	u8 noise_level;
	u8 max_strength;
	u8 hover_id_num;
};

enum tsp_power_mode {
	FTS_POWER_STATE_POWERDOWN = 0,
	FTS_POWER_STATE_LOWPOWER,
	FTS_POWER_STATE_ACTIVE,
//	FTS_POWER_STATE_SLEEP,
};

#define FTS_READ_DEVICE_ID				0x22
#define FTS_READ_FW_VERSION				0x24
#define FTS_CMD_FORCE_CALIBRATION		0x13

#define FTS_READ_ONE_EVENT				0x60
#define FTS_READ_ALL_EVENT				0x61
#define FTS_CMD_CLEAR_ALL_EVENT			0x62

#define FTS_TS_LOCATION_DETECT_SIZE	6
#define FTS_EVENT_SIZE 16
#define FTS_FIFO_MAX 31

/* Status Event */
#define FTS_COORDINATE_EVENT			0
#define FTS_STATUS_EVENT			1
#define FTS_GESTURE_EVENT			2
#define FTS_VENDOR_EVENT			3

#define FTS_COORDINATE_ACTION_NONE		0
#define FTS_COORDINATE_ACTION_PRESS		1
#define FTS_COORDINATE_ACTION_MOVE		2
#define FTS_COORDINATE_ACTION_RELEASE		3

#define FTS_EVENT_TOUCHTYPE_NORMAL		0
#define FTS_EVENT_TOUCHTYPE_HOVER		1
#define FTS_EVENT_TOUCHTYPE_FLIPCOVER		2
#define FTS_EVENT_TOUCHTYPE_GLOVE		3
#define FTS_EVENT_TOUCHTYPE_STYLUS		4
#define FTS_EVENT_TOUCHTYPE_PALM		5
#define FTS_EVENT_TOUCHTYPE_WET			6
#define FTS_EVENT_TOUCHTYPE_PROXIMITY		7
#define FTS_EVENT_TOUCHTYPE_JIG			8

/* Status - ERROR event */
#define FTS_EVENT_STATUSTYPE_CMDDRIVEN		0
#define FTS_EVENT_STATUSTYPE_ERROR		1
#define FTS_EVENT_STATUSTYPE_INFORMATION	2
#define FTS_EVENT_STATUSTYPE_USERINPUT		3
#define FTS_EVENT_STATUSTYPE_VENDORINFO		7

#define FTS_ERR_EVNET_CORE_ERR			0x00
#define FTS_ERR_EVENT_QUEUE_FULL		0x01
#define FTS_ERR_EVENT_ESD			0x02

/* Status - Information report */
#define FTS_INFO_READY_STATUS			0x00
#define FTS_INFO_WET_MODE			0x01
#define FTS_INFO_NOISE_MODE			0x02
#define FTS_INFO_XENOSENSOR_DETECT		0x04


enum stmfts_regulators {
	STMFTS_REGULATOR_VDD,
	STMFTS_REGULATOR_AVDD,
};

struct stmfts_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct mutex mutex;

	struct regulator_bulk_data regulators[2];

	u16 chip_id;
	u8 chip_ver;
	u16 fw_ver;
	u8 config_id;
	u8 config_ver;

	struct fts_finger finger[STMFTS_MAX_FINGERS];
	int touch_count;

	u8 check_multi;
	unsigned int multi_count;
	unsigned int all_finger_count;
};

/************************************************************
*  720  * 1480 : <48 96 60> indicator: 24dp navigator:48dp edge:60px dpi=320
* 1080  * 2220 :  4096 * 4096 : <133 266 341>  (approximately value)
************************************************************/

static void location_detect(char *loc, int x, int y)
{
	int i;

	for (i = 0; i < FTS_TS_LOCATION_DETECT_SIZE; ++i)
		loc[i] = 0;

	if (x < 60)
		strcat(loc, "E.");
	else if (x < (4095 - 60))
		strcat(loc, "C.");
	else
		strcat(loc, "e.");

	if (y < 48)
		strcat(loc, "S");
	else if (y < (4095 - 96))
		strcat(loc, "C");
	else
		strcat(loc, "N");
}

static const char finger_mode[10] = {'N', '1', '2', 'G', '4', 'P'};

//#define I2C_SMBUS_BLOCK_MAX 240
static u8 fts_event_handler_type_b(struct stmfts_data *sdata)
{
	u8 regAdd;
	int left_event_count = 0;
	int EventNum = 0;
	u8 TouchID = 0, event_id = 0;
	u8 data[FTS_FIFO_MAX * FTS_EVENT_SIZE] = {0};
	u8 *event_buff;
	struct fts_event_coordinate *p_event_coord;
	struct fts_gesture_status *p_gesture_status;
	struct fts_event_status *p_event_status;

	u8 prev_action = 0;
	char location[FTS_TS_LOCATION_DETECT_SIZE] = { 0 };

	regAdd = FTS_READ_ONE_EVENT;
	i2c_smbus_read_i2c_block_data(sdata->client, regAdd,
					    FTS_EVENT_SIZE, (u8 *)&data[0 * FTS_EVENT_SIZE]);
	left_event_count = (data[7] & 0x1F);

	if (left_event_count >= FTS_FIFO_MAX)
		left_event_count = FTS_FIFO_MAX - 1;

	if (left_event_count > 0) {
		regAdd = FTS_READ_ALL_EVENT;
		i2c_smbus_read_i2c_block_data(sdata->client, regAdd,
					    FTS_EVENT_SIZE * (left_event_count), (u8 *)&data[1 * FTS_EVENT_SIZE]);
	}

	do {
		/* for event debugging */
		dev_dbg(&sdata->client->dev, "[%d] %02X %02X %02X %02X %02X %02X %02X %02X\n",
				EventNum, data[EventNum * FTS_EVENT_SIZE+0], data[EventNum * FTS_EVENT_SIZE+1],
				data[EventNum * FTS_EVENT_SIZE+2], data[EventNum * FTS_EVENT_SIZE+3],
				data[EventNum * FTS_EVENT_SIZE+4], data[EventNum * FTS_EVENT_SIZE+5],
				data[EventNum * FTS_EVENT_SIZE+6], data[EventNum * FTS_EVENT_SIZE+7]);

		event_buff = (u8 *) &data[EventNum * FTS_EVENT_SIZE];
		event_id = event_buff[0] & 0x3;

		switch (event_id) {
		case FTS_STATUS_EVENT:
			p_event_status = (struct fts_event_status *)event_buff;

			if (p_event_status->stype > 0)
				dev_dbg(&sdata->client->dev, "%s: STATUS %02X %02X %02X %02X %02X %02X %02X %02X\n",
						__func__, event_buff[0], event_buff[1], event_buff[2],
						event_buff[3], event_buff[4], event_buff[5],
						event_buff[6], event_buff[7]);

			if ((p_event_status->stype == FTS_EVENT_STATUSTYPE_ERROR) &&
					(p_event_status->status_id == FTS_ERR_EVENT_QUEUE_FULL)) {
				dev_dbg(&sdata->client->dev, "%s: IC Event Queue is full\n", __func__);
			}

			if ((p_event_status->stype == FTS_EVENT_STATUSTYPE_ERROR) &&
					(p_event_status->status_id == FTS_ERR_EVENT_ESD)) {
				dev_dbg(&sdata->client->dev, "%s: ESD detected. run reset\n", __func__);
			}

			if ((p_event_status->stype == FTS_EVENT_STATUSTYPE_INFORMATION) &&
					(p_event_status->status_id == FTS_INFO_READY_STATUS)) {
				if (p_event_status->status_data_1 == 0x10) {
					dev_dbg(&sdata->client->dev, "%s: IC Reset\n", __func__);
				}
			}
			break;

		case FTS_COORDINATE_EVENT:
			p_event_coord = (struct fts_event_coordinate *) event_buff;

			TouchID = p_event_coord->tid;
			if (TouchID >= STMFTS_MAX_FINGERS) {
				dev_dbg(&sdata->client->dev,
						"%s: tid(%d) is out of supported max finger number\n",
						__func__, TouchID);
				break;
			}

			sdata->finger[TouchID].prev_ttype = sdata->finger[TouchID].ttype;
			prev_action = sdata->finger[TouchID].action;
			sdata->finger[TouchID].id = TouchID;
			sdata->finger[TouchID].action = p_event_coord->tchsta;
			sdata->finger[TouchID].x = (p_event_coord->x_11_4 << 4) | (p_event_coord->x_3_0);
			sdata->finger[TouchID].y = (p_event_coord->y_11_4 << 4) | (p_event_coord->y_3_0);
			sdata->finger[TouchID].z = p_event_coord->z & 0x3F;
			sdata->finger[TouchID].ttype = p_event_coord->ttype_3_2 << 2 |
							p_event_coord->ttype_1_0 << 0;
			sdata->finger[TouchID].major = p_event_coord->major;
			sdata->finger[TouchID].minor = p_event_coord->minor;
			sdata->finger[TouchID].max_energy = p_event_coord->max_energy;
			if (sdata->finger[TouchID].max_energy) {
				sdata->finger[TouchID].max_energy_x = sdata->finger[TouchID].x;
				sdata->finger[TouchID].max_energy_y = sdata->finger[TouchID].y;
			}

			if (!sdata->finger[TouchID].palm &&
					sdata->finger[TouchID].ttype == FTS_EVENT_TOUCHTYPE_PALM)
				sdata->finger[TouchID].palm_count++;

			sdata->finger[TouchID].palm = (sdata->finger[TouchID].ttype == FTS_EVENT_TOUCHTYPE_PALM);
			sdata->finger[TouchID].left_event = p_event_coord->left_event;

			sdata->finger[TouchID].noise_level = p_event_coord->noise_level;
			sdata->finger[TouchID].max_strength = max(sdata->finger[TouchID].max_strength, p_event_coord->max_strength);
			sdata->finger[TouchID].hover_id_num = max(sdata->finger[TouchID].hover_id_num, (u8)p_event_coord->hover_id_num);

			if (sdata->finger[TouchID].z <= 0)
				sdata->finger[TouchID].z = 1;

			if ((sdata->finger[TouchID].ttype == FTS_EVENT_TOUCHTYPE_NORMAL) ||
					(sdata->finger[TouchID].ttype == FTS_EVENT_TOUCHTYPE_PALM)   ||
					(sdata->finger[TouchID].ttype == FTS_EVENT_TOUCHTYPE_WET)    ||
					(sdata->finger[TouchID].ttype == FTS_EVENT_TOUCHTYPE_GLOVE)) {

				location_detect(location, sdata->finger[TouchID].x, sdata->finger[TouchID].y);

				if (sdata->finger[TouchID].action == FTS_COORDINATE_ACTION_RELEASE) {
					input_mt_slot(sdata->input, TouchID);

					/*if (sdata->board->support_mt_pressure)
						input_report_abs(sdata->input, ABS_MT_PRESSURE, 0);*/

					//input_report_abs(sdata->input, ABS_MT_CUSTOM, 0);

					input_mt_report_slot_state(sdata->input, MT_TOOL_FINGER, 0);

					if (sdata->touch_count > 0)
						sdata->touch_count--;

					if (sdata->touch_count == 0) {
						input_report_key(sdata->input, BTN_TOUCH, 0);
						input_report_key(sdata->input, BTN_TOOL_FINGER, 0);
						sdata->check_multi = 0;
					}

					dev_dbg(&sdata->client->dev,
							"[R] tID:%d loc:%s dd:%d,%d mc:%d tc:%d lx:%d ly:%d mx:%d my:%d p:%d nlvl:%d maxS:%d hid:%d\n",
							TouchID, location,
							sdata->finger[TouchID].x - sdata->finger[TouchID].p_x,
							sdata->finger[TouchID].y - sdata->finger[TouchID].p_y,
							sdata->finger[TouchID].mcount, sdata->touch_count,
							sdata->finger[TouchID].x, sdata->finger[TouchID].y,
							sdata->finger[TouchID].max_energy_x, sdata->finger[TouchID].max_energy_y,
							sdata->finger[TouchID].palm_count, sdata->finger[TouchID].noise_level,
							sdata->finger[TouchID].max_strength, sdata->finger[TouchID].hover_id_num);

					sdata->finger[TouchID].action = FTS_COORDINATE_ACTION_NONE;
					sdata->finger[TouchID].mcount = 0;
					sdata->finger[TouchID].palm_count = 0;
					sdata->finger[TouchID].noise_level = 0;
					sdata->finger[TouchID].max_strength = 0;
					sdata->finger[TouchID].hover_id_num = 0;

				} else if (sdata->finger[TouchID].action == FTS_COORDINATE_ACTION_PRESS) {

					sdata->touch_count++;
					sdata->all_finger_count++;

					sdata->finger[TouchID].p_x = sdata->finger[TouchID].x;
					sdata->finger[TouchID].p_y = sdata->finger[TouchID].y;

					input_mt_slot(sdata->input, TouchID);
					input_mt_report_slot_state(sdata->input, MT_TOOL_FINGER, 1);
					input_report_key(sdata->input, BTN_TOUCH, 1);
					input_report_key(sdata->input, BTN_TOOL_FINGER, 1);

					input_report_abs(sdata->input, ABS_MT_POSITION_X, sdata->finger[TouchID].x);
					input_report_abs(sdata->input, ABS_MT_POSITION_Y, sdata->finger[TouchID].y);
					input_report_abs(sdata->input, ABS_MT_TOUCH_MAJOR,
								sdata->finger[TouchID].major);
					input_report_abs(sdata->input, ABS_MT_TOUCH_MINOR,
								sdata->finger[TouchID].minor);

					/*if (sdata->brush_mode)
						input_report_abs(sdata->input, ABS_MT_CUSTOM,
									(sdata->finger[TouchID].max_energy << 16) |
									(sdata->finger[TouchID].z << 1) |
									sdata->finger[TouchID].palm);
					else
						input_report_abs(sdata->input, ABS_MT_CUSTOM,
									(sdata->finger[TouchID].max_energy << 16) |
									(BRUSH_Z_DATA << 1) |
									sdata->finger[TouchID].palm);*/

					/*if (sdata->board->support_mt_pressure)
						input_report_abs(sdata->input, ABS_MT_PRESSURE,
									sdata->finger[TouchID].z);*/

					if ((sdata->touch_count > 4) && (sdata->check_multi == 0)) {
						sdata->check_multi = 1;
						sdata->multi_count++;
					}

					dev_dbg(&sdata->client->dev,
							"[P] tID:%d.%d x:%d y:%d z:%d major:%d minor:%d loc:%s tc:%d type:%d p:%d nlvl:%d maxS:%d hid:%d\n",
							TouchID, (sdata->input->mt->trkid - 1) & TRKID_MAX,
							sdata->finger[TouchID].x, sdata->finger[TouchID].y,
							sdata->finger[TouchID].z,
							sdata->finger[TouchID].major, sdata->finger[TouchID].minor,
							location, sdata->touch_count, sdata->finger[TouchID].ttype,
							sdata->finger[TouchID].palm_count, sdata->finger[TouchID].noise_level,
							sdata->finger[TouchID].max_strength, sdata->finger[TouchID].hover_id_num);

				} else if (sdata->finger[TouchID].action == FTS_COORDINATE_ACTION_MOVE) {
					if (sdata->touch_count == 0) {
						dev_dbg(&sdata->client->dev, "%s: touch count 0\n", __func__);
						break;
					}

					if (prev_action == FTS_COORDINATE_ACTION_NONE) {
						dev_dbg(&sdata->client->dev,
								"%s: previous state is released but point is moved\n",
								__func__);
						break;
					}

					input_mt_slot(sdata->input, TouchID);
					input_mt_report_slot_state(sdata->input, MT_TOOL_FINGER, 1);
					input_report_key(sdata->input, BTN_TOUCH, 1);
					input_report_key(sdata->input, BTN_TOOL_FINGER, 1);

					input_report_abs(sdata->input, ABS_MT_POSITION_X, sdata->finger[TouchID].x);
					input_report_abs(sdata->input, ABS_MT_POSITION_Y, sdata->finger[TouchID].y);
					input_report_abs(sdata->input, ABS_MT_TOUCH_MAJOR,
								sdata->finger[TouchID].major);
					input_report_abs(sdata->input, ABS_MT_TOUCH_MINOR,
								sdata->finger[TouchID].minor);

					/*if (sdata->brush_mode)
						input_report_abs(sdata->input, ABS_MT_CUSTOM,
									(sdata->finger[TouchID].max_energy << 16) |
									(sdata->finger[TouchID].z << 1) |
									sdata->finger[TouchID].palm);
					else
						input_report_abs(sdata->input, ABS_MT_CUSTOM,
									(sdata->finger[TouchID].max_energy << 16) |
									(BRUSH_Z_DATA << 1) |
									sdata->finger[TouchID].palm);*/

					/*if (sdata->board->support_mt_pressure)
						input_report_abs(sdata->input, ABS_MT_PRESSURE,
									sdata->finger[TouchID].z);*/

					sdata->finger[TouchID].mcount++;
				} else {
					dev_dbg(&sdata->client->dev,
							"%s: do not support coordinate action(%d)\n",
							__func__, sdata->finger[TouchID].action);
				}


				if (sdata->finger[TouchID].ttype != sdata->finger[TouchID].prev_ttype) {
					dev_dbg(&sdata->client->dev, "%s: tID:%d ttype(%c->%c) : %s\n",
							__func__, sdata->finger[TouchID].id,
							finger_mode[sdata->finger[TouchID].prev_ttype],
							finger_mode[sdata->finger[TouchID].ttype],
							sdata->finger[TouchID].action == FTS_COORDINATE_ACTION_PRESS ? "P" :
							sdata->finger[TouchID].action == FTS_COORDINATE_ACTION_MOVE ? "M" : "R");
				}
			} else {
				dev_dbg(&sdata->client->dev,
						"%s: do not support coordinate type(%d)\n",
						__func__, sdata->finger[TouchID].ttype);
			}

			break;
		case FTS_GESTURE_EVENT:
			p_gesture_status = (struct fts_gesture_status *)event_buff;
			dev_dbg(&sdata->client->dev, "%s: [GESTURE] type:%X sf:%X id:%X | %X, %X, %X, %X\n",
				__func__, p_gesture_status->stype, p_gesture_status->sf, p_gesture_status->gesture_id,
				p_gesture_status->gesture_data_1, p_gesture_status->gesture_data_2,
				p_gesture_status->gesture_data_3, p_gesture_status->gesture_data_4);
			break;
		case FTS_VENDOR_EVENT: // just print message for debugging
			if (event_buff[1] == 0x01) {  // echo event
				dev_dbg(&sdata->client->dev,
						"%s: echo event %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
						__func__, event_buff[0], event_buff[1], event_buff[2], event_buff[3], event_buff[4], event_buff[5],
						event_buff[6], event_buff[7], event_buff[8], event_buff[9], event_buff[10], event_buff[11],
						event_buff[12], event_buff[13], event_buff[14], event_buff[15]);
			} else {
				dev_dbg(&sdata->client->dev,
						"%s: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
						__func__, event_buff[0], event_buff[1], event_buff[2], event_buff[3], event_buff[4], event_buff[5],
						event_buff[6], event_buff[7], event_buff[8], event_buff[9], event_buff[10], event_buff[11],
						event_buff[12], event_buff[13], event_buff[14], event_buff[15]);
			}
			break;
		default:
			dev_dbg(&sdata->client->dev,
					"%s: unknown event %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
						__func__, event_buff[0], event_buff[1], event_buff[2], event_buff[3], event_buff[4], event_buff[5],
						event_buff[6], event_buff[7], event_buff[8], event_buff[9], event_buff[10], event_buff[11],
						event_buff[12], event_buff[13], event_buff[14], event_buff[15]);
			break;
		}

		EventNum++;
		left_event_count--;
	} while (left_event_count >= 0);

	input_sync(sdata->input);

//	fts_lfd_ctrl(info, sdata->touch_count);

	return 0;
}

static irqreturn_t stmfts_irq_handler(int irq, void *dev)
{
	struct stmfts_data *sdata = dev;

	mutex_lock(&sdata->mutex);

	fts_event_handler_type_b(sdata);

	mutex_unlock(&sdata->mutex);
	return IRQ_HANDLED;
}

static ssize_t stmfts_sysfs_chip_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct stmfts_data *sdata = dev_get_drvdata(dev);

	return sprintf(buf, "%#x\n", sdata->chip_id);
}

static ssize_t stmfts_sysfs_chip_version(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct stmfts_data *sdata = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", sdata->chip_ver);
}

static ssize_t stmfts_sysfs_fw_ver(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct stmfts_data *sdata = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", sdata->fw_ver);
}

static ssize_t stmfts_sysfs_config_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct stmfts_data *sdata = dev_get_drvdata(dev);

	return sprintf(buf, "%#x\n", sdata->config_id);
}

static ssize_t stmfts_sysfs_config_version(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct stmfts_data *sdata = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", sdata->config_ver);
}

static DEVICE_ATTR(chip_id, 0444, stmfts_sysfs_chip_id, NULL);
static DEVICE_ATTR(chip_version, 0444, stmfts_sysfs_chip_version, NULL);
static DEVICE_ATTR(fw_ver, 0444, stmfts_sysfs_fw_ver, NULL);
static DEVICE_ATTR(config_id, 0444, stmfts_sysfs_config_id, NULL);
static DEVICE_ATTR(config_version, 0444, stmfts_sysfs_config_version, NULL);

static struct attribute *stmfts_sysfs_attrs[] = {
	&dev_attr_chip_id.attr,
	&dev_attr_chip_version.attr,
	&dev_attr_fw_ver.attr,
	&dev_attr_config_id.attr,
	&dev_attr_config_version.attr,
	NULL
};

static struct attribute_group stmfts_attribute_group = {
	.attrs = stmfts_sysfs_attrs
};

static int stmfts_power_on(struct stmfts_data *sdata)
{
	int err;
	u8 reg[8];
	u8 resetCmds[6] = { 0xFA, 0x20, 0x00, 0x00, 0x24, 0x81 };
	// (FTS_TOUCHTYPE_BIT_TOUCH | FTS_TOUCHTYPE_BIT_PALM | FTS_TOUCHTYPE_BIT_WET)
	u8 touchCmds[3] = { 0x39, 0x61, 0x00 };
	u8 calCmds[1] = { FTS_CMD_FORCE_CALIBRATION };
	u8 event_clrCmds[1] = { FTS_CMD_CLEAR_ALL_EVENT };
	u8 enScanCmds[3] = { 0xA0, 0x00, 0x01 };

	err = regulator_bulk_enable(ARRAY_SIZE(sdata->regulators),
				    sdata->regulators);
	if (err)
		return err;

	/*
	 * The datasheet does not specify the power on time, but considering
	 * that the reset time is < 10ms, I sleep 20ms to be sure
	 */
	msleep(20);

	err = i2c_smbus_read_i2c_block_data(sdata->client, FTS_READ_FW_VERSION,
					    sizeof(reg), reg);
	if (err < 0)
		return err;
	if (err != sizeof(reg))
		return -EIO;

	sdata->fw_ver = (reg[0] << 8) + reg[1];
	sdata->config_id = 0;
	sdata->config_ver = (reg[2] << 8) + reg[3];

	err = i2c_smbus_read_i2c_block_data(sdata->client, FTS_READ_DEVICE_ID,
					    sizeof(reg), reg);
	if (err < 0)
		return err;
	if (err != sizeof(reg))
		return -EIO;

	sdata->chip_id = (reg[2] << 8) + reg[3];
	sdata->chip_ver = reg[4];

	err = i2c_master_send(sdata->client, resetCmds, 6);
	if (err < 0)
		return err;

	msleep(20);

	enable_irq(sdata->client->irq);

	err = i2c_master_send(sdata->client, touchCmds, 3);
	if (err < 0)
		return err;

	err = i2c_master_send(sdata->client, calCmds, 1);
	if (err < 0)
		return err;

	err = i2c_master_send(sdata->client, event_clrCmds, 1);
	if (err < 0)
		return err;

	err = i2c_master_send(sdata->client, enScanCmds, 3);
	if (err < 0)
		return err;

	return 0;
}

static void stmfts_power_off(void *data)
{
	struct stmfts_data *sdata = data;
	int err;

	u8 disScanCmds[3] = { 0xA0, 0x00, 0x00 };

	err = i2c_master_send(sdata->client, disScanCmds, 3);
	if (err)
		dev_warn(&sdata->client->dev,
			 "failed to disable touchscreen: %d\n", err);

	disable_irq(sdata->client->irq);
	regulator_bulk_disable(ARRAY_SIZE(sdata->regulators),
						sdata->regulators);
}

static int stmfts_probe(struct i2c_client *client)
{
	int err;
	struct stmfts_data *sdata;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
						I2C_FUNC_SMBUS_BYTE_DATA |
						I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENODEV;

	sdata = devm_kzalloc(&client->dev, sizeof(*sdata), GFP_KERNEL);
	if (!sdata)
		return -ENOMEM;

	i2c_set_clientdata(client, sdata);

	sdata->client = client;
	mutex_init(&sdata->mutex);

	sdata->regulators[STMFTS_REGULATOR_VDD].supply = "vdd";
	sdata->regulators[STMFTS_REGULATOR_AVDD].supply = "avdd";
	err = devm_regulator_bulk_get(&client->dev,
				      ARRAY_SIZE(sdata->regulators),
				      sdata->regulators);
	if (err)
		return err;

	sdata->input = devm_input_allocate_device(&client->dev);
	if (!sdata->input)
		return -ENOMEM;

	sdata->input->name = STMFTS_DEV_NAME;
	sdata->input->id.bustype = BUS_I2C;

	input_set_capability(sdata->input, EV_ABS, ABS_MT_POSITION_X);
	input_set_capability(sdata->input, EV_ABS, ABS_MT_POSITION_Y);

	input_set_abs_params(sdata->input, ABS_MT_POSITION_X, 0,
			     4095, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_POSITION_Y, 0,
			     4095, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_ORIENTATION, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_DISTANCE, 0, 255, 0, 0);

	err = input_mt_init_slots(sdata->input,
				  STMFTS_MAX_FINGERS, INPUT_MT_DIRECT);
	if (err)
		return err;

	input_set_drvdata(sdata->input, sdata);

	/*
	 * stmfts_power_on expects interrupt to be disabled, but
	 * at this point the device is still off and I do not trust
	 * the status of the irq line that can generate some spurious
	 * interrupts. To be on the safe side it's better to not enable
	 * the interrupts during their request.
	 */
	err = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, stmfts_irq_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_LOW | IRQF_NO_AUTOEN,
					"stmfts_irq", sdata);
	if (err)
		return err;

	dev_dbg(&client->dev, "initializing ST-Microelectronics FTS...\n");

	err = stmfts_power_on(sdata);
	if (err)
		return err;

	err = devm_add_action_or_reset(&client->dev, stmfts_power_off, sdata);
	if (err)
		return err;

	err = input_register_device(sdata->input);
	if (err)
		return err;

	err = devm_device_add_group(&client->dev, &stmfts_attribute_group);
	if (err)
		return err;

	device_enable_async_suspend(&client->dev);

	return 0;
}

static int __maybe_unused stmfts_suspend(struct device *dev)
{
	struct stmfts_data *sdata = dev_get_drvdata(dev);

	if (input_device_enabled(sdata->input))
		stmfts_power_off(sdata);

	return 0;
}

static int __maybe_unused stmfts_resume(struct device *dev)
{
	struct stmfts_data *sdata = dev_get_drvdata(dev);

	if (input_device_enabled(sdata->input))
		return stmfts_power_on(sdata);

	return 0;
}

static const struct dev_pm_ops stmfts_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stmfts_suspend, stmfts_resume)
};

#ifdef CONFIG_OF
static const struct of_device_id stmfts_of_match[] = {
	{ .compatible = "st,stmfts_fts5cu56a", },
	{ },
};
MODULE_DEVICE_TABLE(of, stmfts_of_match);
#endif

static const struct i2c_device_id stmfts_id[] = {
	{ "stmfts_fts5cu56a", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, stmfts_id);

static struct i2c_driver stmfts_driver = {
	.driver = {
		.name = STMFTS_DEV_NAME,
		.of_match_table = of_match_ptr(stmfts_of_match),
		.pm = &stmfts_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = stmfts_probe,
	.id_table = stmfts_id,
};

module_i2c_driver(stmfts_driver);

MODULE_AUTHOR("Andi Shyti <andi.shyti@samsung.com>");
MODULE_DESCRIPTION("STMicroelectronics FTS Touch Screen");
MODULE_LICENSE("GPL v2");
