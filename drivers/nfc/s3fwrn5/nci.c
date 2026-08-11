// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NCI based driver for Samsung S3FWRN5 NFC chip
 *
 * Copyright (C) 2015 Samsung Electronics
 * Robert Baldyga <r.baldyga@samsung.com>
 */

#include <linux/completion.h>
#include <linux/firmware.h>
#include <linux/minmax.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

#include "s3fwrn5.h"
#include "nci.h"

static int s3fwrn5_nci_prop_rsp(struct nci_dev *ndev, struct sk_buff *skb)
{
	__u8 status = skb->data[0];

	nci_req_complete(ndev, status);
	return 0;
}

/*
 * DUAL_OPTION responses are not uniform: GET_VER answers with the chip's
 * calibration versions instead of a status byte, so stash the payload for
 * the caller before completing the request.
 */
static int s3fwrn5_nci_dual_rsp(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct s3fwrn5_info *info = nci_get_drvdata(ndev);

	info->dual_rsp_len = min_t(unsigned int, skb->len,
				   sizeof(info->dual_rsp));
	memcpy(info->dual_rsp, skb->data, info->dual_rsp_len);

	nci_req_complete(ndev, skb->data[0]);
	return 0;
}

const struct nci_driver_ops s3fwrn5_nci_prop_ops[5] = {
	{
		.opcode = nci_opcode_pack(NCI_GID_PROPRIETARY,
				NCI_PROP_SET_RFREG),
		.rsp = s3fwrn5_nci_prop_rsp,
	},
	{
		.opcode = nci_opcode_pack(NCI_GID_PROPRIETARY,
				NCI_PROP_START_RFREG),
		.rsp = s3fwrn5_nci_prop_rsp,
	},
	{
		.opcode = nci_opcode_pack(NCI_GID_PROPRIETARY,
				NCI_PROP_STOP_RFREG),
		.rsp = s3fwrn5_nci_prop_rsp,
	},
	{
		.opcode = nci_opcode_pack(NCI_GID_PROPRIETARY,
				NCI_PROP_FW_CFG),
		.rsp = s3fwrn5_nci_prop_rsp,
	},
	{
		.opcode = nci_opcode_pack(NCI_GID_PROPRIETARY,
				NCI_PROP_DUAL_OPTION),
		.rsp = s3fwrn5_nci_dual_rsp,
	},
};

#define S3FWRN5_RFREG_SECTION_SIZE 252

int s3fwrn5_nci_rf_configure(struct s3fwrn5_info *info, const char *fw_name)
{
	struct device *dev = &info->ndev->nfc_dev->dev;
	const struct firmware *fw;
	struct nci_prop_fw_cfg_cmd fw_cfg;
	struct nci_prop_set_rfreg_cmd set_rfreg;
	struct nci_prop_stop_rfreg_cmd stop_rfreg;
	u32 checksum;
	int i, len;
	int ret;

	ret = request_firmware(&fw, fw_name, dev);
	if (ret < 0)
		return ret;

	/* Compute rfreg checksum */

	checksum = 0;
	for (i = 0; i < fw->size; i += 4)
		checksum += *((u32 *)(fw->data+i));

	/* Set default clock configuration for external crystal */

	fw_cfg.clk_type = 0x01;
	fw_cfg.clk_speed = 0xff;
	fw_cfg.clk_req = 0xff;
	ret = nci_prop_cmd(info->ndev, NCI_PROP_FW_CFG,
		sizeof(fw_cfg), (__u8 *)&fw_cfg);
	if (ret < 0)
		goto out;

	/* Start rfreg configuration */

	dev_info(dev, "rfreg configuration update: %s\n", fw_name);

	ret = nci_prop_cmd(info->ndev, NCI_PROP_START_RFREG, 0, NULL);
	if (ret < 0) {
		dev_err(dev, "Unable to start rfreg update\n");
		goto out;
	}

	/* Update rfreg */

	set_rfreg.index = 0;
	for (i = 0; i < fw->size; i += S3FWRN5_RFREG_SECTION_SIZE) {
		len = (fw->size - i < S3FWRN5_RFREG_SECTION_SIZE) ?
			(fw->size - i) : S3FWRN5_RFREG_SECTION_SIZE;
		memcpy(set_rfreg.data, fw->data+i, len);
		ret = nci_prop_cmd(info->ndev, NCI_PROP_SET_RFREG,
			len+1, (__u8 *)&set_rfreg);
		if (ret < 0) {
			dev_err(dev, "rfreg update error (code=%d)\n", ret);
			goto out;
		}
		set_rfreg.index++;
	}

	/* Finish rfreg configuration */

	stop_rfreg.checksum = checksum & 0xffff;
	ret = nci_prop_cmd(info->ndev, NCI_PROP_STOP_RFREG,
		sizeof(stop_rfreg), (__u8 *)&stop_rfreg);
	if (ret < 0) {
		dev_err(dev, "Unable to stop rfreg update\n");
		goto out;
	}

	dev_info(dev, "rfreg configuration update: success\n");
out:
	release_firmware(fw);
	return ret;
}

/*
 * The S3NRN4V expects the single-byte FW_CFG form (just the clock-speed
 * selector).
 */
int s3fwrn5_nci_clk_cfg(struct s3fwrn5_info *info)
{
	u8 clk_speed = NCI_PROP_FW_CFG_CLK_SPEED;

	return nci_prop_cmd(info->ndev, NCI_PROP_FW_CFG, 1, &clk_speed);
}

/*
 * An 8-byte calibration version: 5 bytes of date stamp and a 3-byte CSC
 * code, at fixed offsets both in a blob's 16-byte tail and in each half of
 * the GET_VER response (HW at offset 0, SW at offset 15).
 */
static bool s3fwrn5_nci_dual_version_eq(const u8 *a, const u8 *b)
{
	return !memcmp(a + 5, b + 5, 5) && !memcmp(a + 12, b + 12, 3);
}

static bool s3fwrn5_nci_dual_cal_current(struct s3fwrn5_info *info,
					 const struct firmware *hw_fw,
					 const struct firmware *sw_fw)
{
	if (info->dual_rsp_len < 30)
		return false;
	if (hw_fw->size < 16 || sw_fw->size < 16)
		return false;

	return s3fwrn5_nci_dual_version_eq(info->dual_rsp,
					   hw_fw->data + hw_fw->size - 16) &&
	       s3fwrn5_nci_dual_version_eq(info->dual_rsp + 15,
					   sw_fw->data + sw_fw->size - 16);
}

/*
 * S3NRN4V RF calibration data update: the HW and SW blobs merged into one
 * stream (HW first), pushed as START_UPDATE, one SET_OPTION per 252-byte
 * section, then STOP_UPDATE carrying a 16-bit checksum (running sum of the
 * merged stream as 32-bit words).
 */
int s3fwrn5_nci_rf_configure_dual(struct s3fwrn5_info *info,
				  const char *hw_name, const char *sw_name)
{
	struct nci_prop_dual_set_option_cmd set_option;
	struct device *dev = &info->ndev->nfc_dev->dev;
	const struct firmware *hw_fw, *sw_fw;
	size_t merged_size, i, len;
	u8 *merged;
	u8 stop_cmd[3];
	u32 checksum;
	u8 sub_oid;
	int ret;

	ret = firmware_request_nowarn(&hw_fw, hw_name, dev);
	if (ret < 0)
		return ret;
	ret = firmware_request_nowarn(&sw_fw, sw_name, dev);
	if (ret < 0)
		goto out_hw;

	merged_size = hw_fw->size + sw_fw->size;

	/*
	 * The stream is checksummed as 32-bit words and pushed in at most 256
	 * sections (the section index is a single byte); reject blobs that
	 * would silently break either.
	 */
	if (!merged_size || merged_size % 4 ||
	    merged_size > 256 * NCI_PROP_DUAL_SECTION_SIZE) {
		dev_err(dev, "invalid calibration data size: %zu\n", merged_size);
		ret = -EINVAL;
		goto out_sw;
	}

	/*
	 * Ask the chip for its current calibration versions and skip the
	 * upload when both already match the blobs; a mismatch or an
	 * unparseable answer means the upload proceeds. GET_VER answers with
	 * versions, not a status byte, so nci_prop_cmd()'s return carries no
	 * meaning here.
	 */
	sub_oid = NCI_PROP_DUAL_SUB_GET_VER;
	info->dual_rsp_len = 0;
	nci_prop_cmd(info->ndev, NCI_PROP_DUAL_OPTION, 1, &sub_oid);
	if (s3fwrn5_nci_dual_cal_current(info, hw_fw, sw_fw)) {
		dev_dbg(dev, "calibration data already current\n");
		ret = 0;
		goto out_sw;
	}

	merged = kvmalloc(merged_size, GFP_KERNEL);
	if (!merged) {
		ret = -ENOMEM;
		goto out_sw;
	}
	memcpy(merged, hw_fw->data, hw_fw->size);
	memcpy(merged + hw_fw->size, sw_fw->data, sw_fw->size);

	checksum = 0;
	for (i = 0; i + 4 <= merged_size; i += 4)
		checksum += get_unaligned_le32(merged + i);

	/* START_UPDATE */
	sub_oid = NCI_PROP_DUAL_SUB_START_UPDATE;
	ret = nci_prop_cmd(info->ndev, NCI_PROP_DUAL_OPTION, 1, &sub_oid);
	if (ret < 0) {
		dev_err(dev, "Unable to start calibration data update\n");
		goto out;
	}

	/* SET_OPTION per section */
	set_option.sub_oid = NCI_PROP_DUAL_SUB_SET_OPTION;
	set_option.index = 0;
	for (i = 0; i < merged_size; i += NCI_PROP_DUAL_SECTION_SIZE) {
		len = min_t(size_t, merged_size - i, NCI_PROP_DUAL_SECTION_SIZE);
		memcpy(set_option.data, merged + i, len);
		ret = nci_prop_cmd(info->ndev, NCI_PROP_DUAL_OPTION,
				   len + 2, (__u8 *)&set_option);
		if (ret < 0) {
			dev_err(dev, "calibration data update error: %d\n",
				ret);
			/* Abort form: STOP_UPDATE with the sub-OID alone. */
			sub_oid = NCI_PROP_DUAL_SUB_STOP_UPDATE;
			nci_prop_cmd(info->ndev, NCI_PROP_DUAL_OPTION, 1,
				     &sub_oid);
			goto out;
		}
		set_option.index++;
	}

	/* STOP_UPDATE with checksum */
	stop_cmd[0] = NCI_PROP_DUAL_SUB_STOP_UPDATE;
	put_unaligned_le16(checksum, &stop_cmd[1]);
	ret = nci_prop_cmd(info->ndev, NCI_PROP_DUAL_OPTION, 3, stop_cmd);
	if (ret < 0) {
		dev_err(dev, "Unable to stop calibration data update\n");
		goto out;
	}

	dev_dbg(dev, "calibration data update: success\n");
out:
	kvfree(merged);
out_sw:
	release_firmware(sw_fw);
out_hw:
	release_firmware(hw_fw);
	return ret;
}
