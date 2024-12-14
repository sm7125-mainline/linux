// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Vitalii Skorkin <nikroksm@mail.ru>
 */

#include <linux/unaligned.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define to_ov16a1q(_sd) container_of(_sd, struct ov16a1q, sd)

struct ov16a1q_reg {
	u16 address;
	u32 val;
};

struct ov16a1q_reg_list {
	u32 num_of_regs;
	const struct ov16a1q_reg *regs;
};

struct ov16a1q_mode {
	u32 width;
	u32 height;
	u32 hts;
	u32 vts;
	s64 link_freq;
	u32 lane_count;
	u32 depth;
	const struct ov16a1q_reg_list reg_list;
	u32 mbus_code;
};

static const struct ov16a1q_reg ov16a1q_regs[] = {
	{0x0103, 0x0001},
	{0x0102, 0x0000},
	{0x0301, 0x0048},
	{0x0302, 0x0031},
	{0x0303, 0x0004},
	{0x0305, 0x00c2},
	{0x0306, 0x0000},
	{0x0320, 0x0002},
	{0x0323, 0x0005},
	{0x0324, 0x0002},
	{0x0325, 0x00ee},
	{0x0326, 0x00d8},
	{0x0327, 0x000b},
	{0x0329, 0x0001},
	{0x0343, 0x0004},
	{0x0344, 0x0001},
	{0x0345, 0x0077},
	{0x0346, 0x00c0},
	{0x034a, 0x0007},
	{0x300e, 0x0022},
	{0x3012, 0x0041},
	{0x3016, 0x00d2},
	{0x3018, 0x0070},
	{0x301e, 0x0098},
	{0x3025, 0x0003},
	{0x3026, 0x0010},
	{0x3027, 0x0008},
	{0x3102, 0x0000},
	{0x3400, 0x0004},
	{0x3406, 0x0004},
	{0x3408, 0x0004},
	{0x3421, 0x0009},
	{0x3422, 0x0020},
	{0x3423, 0x0015},
	{0x3424, 0x0040},
	{0x3425, 0x0014},
	{0x3426, 0x0004},
	{0x3504, 0x0008},
	{0x3508, 0x0001},
	{0x3509, 0x0000},
	{0x350a, 0x0001},
	{0x350b, 0x0000},
	{0x350c, 0x0000},
	{0x3548, 0x0001},
	{0x3549, 0x0000},
	{0x354a, 0x0001},
	{0x354b, 0x0000},
	{0x354c, 0x0000},
	{0x3600, 0x00ff},
	{0x3602, 0x0042},
	{0x3603, 0x007b},
	{0x3608, 0x009b},
	{0x360a, 0x0069},
	{0x360b, 0x0053},
	{0x3618, 0x00c0},
	{0x361a, 0x008b},
	{0x361d, 0x0020},
	{0x361e, 0x0030},
	{0x361f, 0x0001},
	{0x3620, 0x0089},
	{0x3624, 0x008f},
	{0x3629, 0x0009},
	{0x362e, 0x0050},
	{0x3631, 0x00e2},
	{0x3632, 0x00e2},
	{0x3634, 0x0010},
	{0x3635, 0x0010},
	{0x3636, 0x0010},
	{0x3639, 0x00a6},
	{0x363a, 0x00aa},
	{0x363b, 0x000c},
	{0x363c, 0x0016},
	{0x363d, 0x0029},
	{0x363e, 0x004f},
	{0x3642, 0x00a8},
	{0x3652, 0x0000},
	{0x3653, 0x0000},
	{0x3654, 0x008a},
	{0x3656, 0x000c},
	{0x3657, 0x008e},
	{0x3660, 0x0080},
	{0x3663, 0x0000},
	{0x3664, 0x0000},
	{0x3668, 0x0005},
	{0x3669, 0x0005},
	{0x370d, 0x0010},
	{0x370e, 0x0005},
	{0x370f, 0x0010},
	{0x3711, 0x0001},
	{0x3712, 0x0009},
	{0x3713, 0x0040},
	{0x3714, 0x00e4},
	{0x3716, 0x0004},
	{0x3717, 0x0001},
	{0x3718, 0x0002},
	{0x3719, 0x0001},
	{0x371a, 0x0002},
	{0x371b, 0x0002},
	{0x371c, 0x0001},
	{0x371d, 0x0002},
	{0x371e, 0x0012},
	{0x371f, 0x0002},
	{0x3720, 0x0014},
	{0x3721, 0x0012},
	{0x3722, 0x0044},
	{0x3723, 0x0060},
	{0x372f, 0x0034},
	{0x3726, 0x0021},
	{0x37d0, 0x0002},
	{0x37d1, 0x0010},
	{0x37db, 0x0008},
	{0x3808, 0x0012},
	{0x3809, 0x0030},
	{0x380a, 0x000d},
	{0x380b, 0x00a8},
	{0x380c, 0x0003},
	{0x380d, 0x0052},
	{0x380e, 0x000f},
	{0x380f, 0x0051},
	{0x3814, 0x0011},
	{0x3815, 0x0011},
	{0x3820, 0x0000},
	{0x3821, 0x0006},
	{0x3822, 0x0000},
	{0x3823, 0x0004},
	{0x3837, 0x0010},
	{0x383c, 0x0034},
	{0x383d, 0x00ff},
	{0x383e, 0x000d},
	{0x383f, 0x0022},
	{0x3857, 0x0000},
	{0x388f, 0x0000},
	{0x3890, 0x0000},
	{0x3891, 0x0000},
	{0x3d81, 0x0010},
	{0x3d83, 0x000c},
	{0x3d84, 0x0000},
	{0x3d85, 0x001b},
	{0x3d88, 0x0000},
	{0x3d89, 0x0000},
	{0x3d8a, 0x0000},
	{0x3d8b, 0x0001},
	{0x3d8c, 0x0077},
	{0x3d8d, 0x00a0},
	{0x3f00, 0x0002},
	{0x3f0c, 0x0007},
	{0x3f0d, 0x002f},
	{0x4012, 0x000d},
	{0x4015, 0x0004},
	{0x4016, 0x001b},
	{0x4017, 0x0004},
	{0x4018, 0x000b},
	{0x401b, 0x001f},
	{0x401e, 0x0001},
	{0x401f, 0x0038},
	{0x4500, 0x0020},
	{0x4501, 0x006a},
	{0x4502, 0x00b4},
	{0x4586, 0x0000},
	{0x4588, 0x0002},
	{0x4640, 0x0001},
	{0x4641, 0x0004},
	{0x4643, 0x0000},
	{0x4645, 0x0003},
	{0x4806, 0x0040},
	{0x480e, 0x0000},
	{0x4815, 0x002b},
	{0x481b, 0x003c},
	{0x4833, 0x0018},
	{0x4837, 0x0008},
	{0x484b, 0x0007},
	{0x4850, 0x0041},
	{0x4860, 0x0000},
	{0x4861, 0x00ec},
	{0x4864, 0x0000},
	{0x4883, 0x0000},
	{0x4888, 0x0010},
	{0x4a00, 0x0010},
	{0x4e00, 0x0000},
	{0x4e01, 0x0004},
	{0x4e02, 0x0001},
	{0x4e03, 0x0000},
	{0x4e04, 0x0008},
	{0x4e05, 0x0004},
	{0x4e06, 0x0000},
	{0x4e07, 0x0013},
	{0x4e08, 0x0001},
	{0x4e09, 0x0000},
	{0x4e0a, 0x0015},
	{0x4e0b, 0x000e},
	{0x4e0c, 0x0000},
	{0x4e0d, 0x0017},
	{0x4e0e, 0x0007},
	{0x4e0f, 0x0000},
	{0x4e10, 0x0019},
	{0x4e11, 0x0006},
	{0x4e12, 0x0000},
	{0x4e13, 0x001b},
	{0x4e14, 0x0008},
	{0x4e15, 0x0000},
	{0x4e16, 0x001f},
	{0x4e17, 0x0008},
	{0x4e18, 0x0000},
	{0x4e19, 0x0021},
	{0x4e1a, 0x000e},
	{0x4e1b, 0x0000},
	{0x4e1c, 0x002d},
	{0x4e1d, 0x0030},
	{0x4e1e, 0x0000},
	{0x4e1f, 0x006a},
	{0x4e20, 0x0005},
	{0x4e21, 0x0000},
	{0x4e22, 0x006c},
	{0x4e23, 0x0005},
	{0x4e24, 0x0000},
	{0x4e25, 0x006e},
	{0x4e26, 0x0039},
	{0x4e27, 0x0000},
	{0x4e28, 0x007a},
	{0x4e29, 0x006d},
	{0x4e2a, 0x0000},
	{0x4e2b, 0x0000},
	{0x4e2c, 0x0000},
	{0x4e2d, 0x0000},
	{0x4e2e, 0x0000},
	{0x4e2f, 0x0000},
	{0x4e30, 0x0000},
	{0x4e31, 0x0000},
	{0x4e32, 0x0000},
	{0x4e33, 0x0000},
	{0x4e34, 0x0000},
	{0x4e35, 0x0000},
	{0x4e36, 0x0000},
	{0x4e37, 0x0000},
	{0x4e38, 0x0000},
	{0x4e39, 0x0000},
	{0x4e3a, 0x0000},
	{0x4e3b, 0x0000},
	{0x4e3c, 0x0000},
	{0x4e3d, 0x0000},
	{0x4e3e, 0x0000},
	{0x4e3f, 0x0000},
	{0x4e40, 0x0000},
	{0x4e41, 0x0000},
	{0x4e42, 0x0000},
	{0x4e43, 0x0000},
	{0x4e44, 0x0000},
	{0x4e45, 0x0000},
	{0x4e46, 0x0000},
	{0x4e47, 0x0000},
	{0x4e48, 0x0000},
	{0x4e49, 0x0000},
	{0x4e4a, 0x0000},
	{0x4e4b, 0x0000},
	{0x4e4c, 0x0000},
	{0x4e4d, 0x0000},
	{0x4e4e, 0x0000},
	{0x4e4f, 0x0000},
	{0x4e50, 0x0000},
	{0x4e51, 0x0000},
	{0x4e52, 0x0000},
	{0x4e53, 0x0000},
	{0x4e54, 0x0000},
	{0x4e55, 0x0000},
	{0x4e56, 0x0000},
	{0x4e57, 0x0000},
	{0x4e58, 0x0000},
	{0x4e59, 0x0000},
	{0x4e5a, 0x0000},
	{0x4e5b, 0x0000},
	{0x4e5c, 0x0000},
	{0x4e5d, 0x0000},
	{0x4e5e, 0x0000},
	{0x4e5f, 0x0000},
	{0x4e60, 0x0000},
	{0x4e61, 0x0000},
	{0x4e62, 0x0000},
	{0x4e63, 0x0000},
	{0x4e64, 0x0000},
	{0x4e65, 0x0000},
	{0x4e66, 0x0000},
	{0x4e67, 0x0000},
	{0x4e68, 0x0000},
	{0x4e69, 0x0000},
	{0x4e6a, 0x0000},
	{0x4e6b, 0x0000},
	{0x4e6c, 0x0000},
	{0x4e6d, 0x0000},
	{0x4e6e, 0x0000},
	{0x4e6f, 0x0000},
	{0x4e70, 0x0000},
	{0x4e71, 0x0000},
	{0x4e72, 0x0000},
	{0x4e73, 0x0000},
	{0x4e74, 0x0000},
	{0x4e75, 0x0000},
	{0x4e76, 0x0000},
	{0x4e77, 0x0000},
	{0x4e78, 0x001c},
	{0x4e79, 0x001e},
	{0x4e7a, 0x0000},
	{0x4e7b, 0x0000},
	{0x4e7c, 0x002c},
	{0x4e7d, 0x002f},
	{0x4e7e, 0x0079},
	{0x4e7f, 0x007b},
	{0x4e80, 0x000a},
	{0x4e81, 0x0031},
	{0x4e82, 0x0066},
	{0x4e83, 0x0081},
	{0x4e84, 0x0003},
	{0x4e85, 0x0040},
	{0x4e86, 0x0002},
	{0x4e87, 0x0009},
	{0x4e88, 0x0043},
	{0x4e89, 0x0053},
	{0x4e8a, 0x0032},
	{0x4e8b, 0x0067},
	{0x4e8c, 0x0005},
	{0x4e8d, 0x0083},
	{0x4e8e, 0x0000},
	{0x4e8f, 0x0000},
	{0x4e90, 0x0000},
	{0x4e91, 0x0000},
	{0x4e92, 0x0000},
	{0x4e93, 0x0000},
	{0x4e94, 0x0000},
	{0x4e95, 0x0000},
	{0x4e96, 0x0000},
	{0x4e97, 0x0000},
	{0x4e98, 0x0000},
	{0x4e99, 0x0000},
	{0x4e9a, 0x0000},
	{0x4e9b, 0x0000},
	{0x4e9c, 0x0000},
	{0x4e9d, 0x0000},
	{0x4e9e, 0x0000},
	{0x4e9f, 0x0000},
	{0x4ea0, 0x0000},
	{0x4ea1, 0x0000},
	{0x4ea2, 0x0000},
	{0x4ea3, 0x0000},
	{0x4ea4, 0x0000},
	{0x4ea5, 0x0000},
	{0x4ea6, 0x001e},
	{0x4ea7, 0x0020},
	{0x4ea8, 0x0032},
	{0x4ea9, 0x006d},
	{0x4eaa, 0x0018},
	{0x4eab, 0x007f},
	{0x4eac, 0x0000},
	{0x4ead, 0x0000},
	{0x4eae, 0x007c},
	{0x4eaf, 0x0007},
	{0x4eb0, 0x007c},
	{0x4eb1, 0x0007},
	{0x4eb2, 0x0007},
	{0x4eb3, 0x001c},
	{0x4eb4, 0x0007},
	{0x4eb5, 0x001c},
	{0x4eb6, 0x0007},
	{0x4eb7, 0x001c},
	{0x4eb8, 0x0007},
	{0x4eb9, 0x001c},
	{0x4eba, 0x0007},
	{0x4ebb, 0x0014},
	{0x4ebc, 0x0007},
	{0x4ebd, 0x001c},
	{0x4ebe, 0x0007},
	{0x4ebf, 0x001c},
	{0x4ec0, 0x0007},
	{0x4ec1, 0x001c},
	{0x4ec2, 0x0007},
	{0x4ec3, 0x001c},
	{0x4ec4, 0x002c},
	{0x4ec5, 0x002f},
	{0x4ec6, 0x0079},
	{0x4ec7, 0x007b},
	{0x4ec8, 0x007c},
	{0x4ec9, 0x0007},
	{0x4eca, 0x007c},
	{0x4ecb, 0x0007},
	{0x4ecc, 0x0000},
	{0x4ecd, 0x0000},
	{0x4ece, 0x0007},
	{0x4ecf, 0x0031},
	{0x4ed0, 0x0069},
	{0x4ed1, 0x007f},
	{0x4ed2, 0x0067},
	{0x4ed3, 0x0000},
	{0x4ed4, 0x0000},
	{0x4ed5, 0x0000},
	{0x4ed6, 0x007c},
	{0x4ed7, 0x0007},
	{0x4ed8, 0x007c},
	{0x4ed9, 0x0007},
	{0x4eda, 0x0033},
	{0x4edb, 0x007f},
	{0x4edc, 0x0000},
	{0x4edd, 0x0016},
	{0x4ede, 0x0000},
	{0x4edf, 0x0000},
	{0x4ee0, 0x0032},
	{0x4ee1, 0x0070},
	{0x4ee2, 0x0001},
	{0x4ee3, 0x0030},
	{0x4ee4, 0x0022},
	{0x4ee5, 0x0028},
	{0x4ee6, 0x006f},
	{0x4ee7, 0x0075},
	{0x4ee8, 0x0000},
	{0x4ee9, 0x0000},
	{0x4eea, 0x0030},
	{0x4eeb, 0x007f},
	{0x4eec, 0x0000},
	{0x4eed, 0x0000},
	{0x4eee, 0x0000},
	{0x4eef, 0x0000},
	{0x4ef0, 0x0069},
	{0x4ef1, 0x007f},
	{0x4ef2, 0x0007},
	{0x4ef3, 0x0030},
	{0x4ef4, 0x0032},
	{0x4ef5, 0x0009},
	{0x4ef6, 0x007d},
	{0x4ef7, 0x0065},
	{0x4ef8, 0x0000},
	{0x4ef9, 0x0000},
	{0x4efa, 0x0000},
	{0x4efb, 0x0000},
	{0x4efc, 0x007f},
	{0x4efd, 0x0009},
	{0x4efe, 0x007f},
	{0x4eff, 0x0009},
	{0x4f00, 0x001e},
	{0x4f01, 0x007c},
	{0x4f02, 0x007f},
	{0x4f03, 0x0009},
	{0x4f04, 0x007f},
	{0x4f05, 0x000b},
	{0x4f06, 0x007c},
	{0x4f07, 0x0002},
	{0x4f08, 0x007c},
	{0x4f09, 0x0002},
	{0x4f0a, 0x0032},
	{0x4f0b, 0x0064},
	{0x4f0c, 0x0032},
	{0x4f0d, 0x0064},
	{0x4f0e, 0x0032},
	{0x4f0f, 0x0064},
	{0x4f10, 0x0032},
	{0x4f11, 0x0064},
	{0x4f12, 0x0031},
	{0x4f13, 0x004f},
	{0x4f14, 0x0083},
	{0x4f15, 0x0084},
	{0x4f16, 0x0063},
	{0x4f17, 0x0064},
	{0x4f18, 0x0083},
	{0x4f19, 0x0084},
	{0x4f1a, 0x0031},
	{0x4f1b, 0x0032},
	{0x4f1c, 0x007b},
	{0x4f1d, 0x007c},
	{0x4f1e, 0x002f},
	{0x4f1f, 0x0030},
	{0x4f20, 0x0030},
	{0x4f21, 0x0069},
	{0x4d06, 0x0008},
	{0x5000, 0x0001},
	{0x5001, 0x0040},
	{0x5002, 0x0053},
	{0x5003, 0x0042},
	{0x5005, 0x0000},
	{0x5038, 0x0000},
	{0x5081, 0x0000},
	{0x5180, 0x0000},
	{0x5181, 0x0010},
	{0x5182, 0x0007},
	{0x5183, 0x008f},
	{0x5820, 0x00c5},
	{0x5854, 0x0000},
	{0x58cb, 0x0003},
	{0x5bd0, 0x0015},
	{0x5bd1, 0x0002},
	{0x5c0e, 0x0011},
	{0x5c11, 0x0000},
	{0x5c16, 0x0002},
	{0x5c17, 0x0001},
	{0x5c1a, 0x0004},
	{0x5c1b, 0x0003},
	{0x5c21, 0x0010},
	{0x5c22, 0x0010},
	{0x5c23, 0x0004},
	{0x5c24, 0x000c},
	{0x5c25, 0x0004},
	{0x5c26, 0x000c},
	{0x5c27, 0x0004},
	{0x5c28, 0x000c},
	{0x5c29, 0x0004},
	{0x5c2a, 0x000c},
	{0x5c2b, 0x0001},
	{0x5c2c, 0x0001},
	{0x5c2e, 0x0008},
	{0x5c30, 0x0004},
	{0x5c35, 0x0003},
	{0x5c36, 0x0003},
	{0x5c37, 0x0003},
	{0x5c38, 0x0003},
	{0x5d00, 0x00ff},
	{0x5d01, 0x000f},
	{0x5d02, 0x0080},
	{0x5d03, 0x0044},
	{0x5d05, 0x00fc},
	{0x5d06, 0x000b},
	{0x5d08, 0x0010},
	{0x5d09, 0x0010},
	{0x5d0a, 0x0004},
	{0x5d0b, 0x000c},
	{0x5d0c, 0x0004},
	{0x5d0d, 0x000c},
	{0x5d0e, 0x0004},
	{0x5d0f, 0x000c},
	{0x5d10, 0x0004},
	{0x5d11, 0x000c},
	{0x5d12, 0x0001},
	{0x5d13, 0x0001},
	{0x5d15, 0x0010},
	{0x5d16, 0x0010},
	{0x5d17, 0x0010},
	{0x5d18, 0x0010},
	{0x5d1a, 0x0010},
	{0x5d1b, 0x0010},
	{0x5d1c, 0x0010},
	{0x5d1d, 0x0010},
	{0x5d1e, 0x0004},
	{0x5d1f, 0x0004},
	{0x5d20, 0x0004},
	{0x5d27, 0x0064},
	{0x5d28, 0x00c8},
	{0x5d29, 0x0096},
	{0x5d2a, 0x00ff},
	{0x5d2b, 0x00c8},
	{0x5d2c, 0x00ff},
	{0x5d2d, 0x0004},
	{0x5d34, 0x0000},
	{0x5d35, 0x0008},
	{0x5d36, 0x0000},
	{0x5d37, 0x0004},
	{0x5d4a, 0x0000},
	{0x5d4c, 0x0000},
};

static const struct ov16a1q_reg ov16a1q_2304x1728_4lane_regs[] = {
	{0x0305, 0x00e1},
	{0x0307, 0x0001},
	{0x4837, 0x0014},
	{0x0329, 0x0001},
	{0x0344, 0x0001},
	{0x0345, 0x0077},
	{0x034a, 0x0007},
	{0x3608, 0x0075},
	{0x360a, 0x0069},
	{0x361a, 0x008b},
	{0x361e, 0x0030},
	{0x3639, 0x0093},
	{0x363a, 0x0099},
	{0x3642, 0x0098},
	{0x3654, 0x008a},
	{0x3656, 0x000c},
	{0x3663, 0x0001},
	{0x370e, 0x0005},
	{0x3712, 0x0008},
	{0x3713, 0x00c0},
	{0x3714, 0x00e2},
	{0x37d0, 0x0002},
	{0x37d1, 0x0010},
	{0x37db, 0x0004},
	{0x3808, 0x0009},
	{0x3809, 0x0000},
	{0x380a, 0x0006},
	{0x380b, 0x00c0},
	{0x380c, 0x0003},
	{0x380d, 0x0052},
	{0x380e, 0x000f},
	{0x380f, 0x0050},
	{0x3814, 0x0022},
	{0x3815, 0x0022},
	{0x3820, 0x0001},
	{0x3821, 0x000c},
	{0x3822, 0x0000},
	{0x383c, 0x0022},
	{0x383f, 0x0033},
	{0x4015, 0x0002},
	{0x4016, 0x000d},
	{0x4017, 0x0000},
	{0x4018, 0x0007},
	{0x401b, 0x001f},
	{0x401f, 0x00fe},
	{0x4500, 0x0020},
	{0x4501, 0x006a},
	{0x4502, 0x00e4},
	{0x4e05, 0x0004},
	{0x4e11, 0x0006},
	{0x4e1d, 0x0025},
	{0x4e26, 0x0044},
	{0x4e29, 0x006d},
	{0x5000, 0x0009},
	{0x5001, 0x0042},
	{0x5003, 0x0042},
	{0x5820, 0x00c5},
	{0x5854, 0x0000},
	{0x5bd0, 0x0019},
	{0x5c0e, 0x0013},
	{0x5c11, 0x0000},
	{0x5c16, 0x0001},
	{0x5c17, 0x0000},
	{0x5c1a, 0x0000},
	{0x5c1b, 0x0000},
	{0x5c21, 0x0008},
	{0x5c22, 0x0008},
	{0x5c23, 0x0002},
	{0x5c24, 0x0006},
	{0x5c25, 0x0002},
	{0x5c26, 0x0006},
	{0x5c27, 0x0002},
	{0x5c28, 0x0006},
	{0x5c29, 0x0002},
	{0x5c2a, 0x0006},
	{0x5c2b, 0x0000},
	{0x5c2c, 0x0000},
	{0x5d01, 0x0007},
	{0x5d08, 0x0008},
	{0x5d09, 0x0008},
	{0x5d0a, 0x0002},
	{0x5d0b, 0x0006},
	{0x5d0c, 0x0002},
	{0x5d0d, 0x0006},
	{0x5d0e, 0x0002},
	{0x5d0f, 0x0006},
	{0x5d10, 0x0002},
	{0x5d11, 0x0006},
	{0x5d12, 0x0000},
	{0x5d13, 0x0000},
	{0x3500, 0x0000},
	{0x3501, 0x0007},
	{0x3502, 0x003c},
	{0x3508, 0x0001},
	{0x3509, 0x0000},
};

static struct ov16a1q_mode ov16a1q_modes[] = {
	{
		.width = 2304,
		.height = 1728,
		.hts = 2550,
		.vts = 3920,
		.link_freq = 180000000,
		.lane_count = 4,
		.depth = 10,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(ov16a1q_2304x1728_4lane_regs),
			.regs = ov16a1q_2304x1728_4lane_regs,
		},
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
	},
};

static const char * const ov16a1q_supply_names[] = {
	"vana",
	"vdig",
	"vio",
};

struct ov16a1q {
	struct clk *xvclk;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct ov16a1q_mode *cur_mode;
	struct regulator_bulk_data supplies[ARRAY_SIZE(ov16a1q_supply_names)];
	struct gpio_desc *reset_gpio;
};

static int ov16a1q_write(struct ov16a1q *ov16a1q, u16 reg, u16 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov16a1q->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << 8 * (4 - len), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2) {
		dev_err(&client->dev,
				"Cannot write register %u!\n", reg);
		return -EIO;
	}

	return 0;
}

static int ov16a1q_write_reg_list(struct ov16a1q *ov16a1q, const struct ov16a1q_reg_list *reg_list)
{
	int ret = 0;

	for (unsigned int i = 0; i < reg_list->num_of_regs; i++)
		ret = ov16a1q_write(ov16a1q, reg_list->regs[i].address, 1, reg_list->regs[i].val);

	return ret;
}

static int ov16a1q_read(struct ov16a1q *ov16a1q, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov16a1q->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2];
	u8 data_buf[4] = {0};
	int ret;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(addr_buf);
	msgs[0].buf = addr_buf;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev,
				"Cannot read register %u!\n", reg);
		return -EIO;
	}

	*val = get_unaligned_be32(data_buf);

	return 0;
}

static int ov16a1q_start_stream(struct ov16a1q *ov16a1q,
				   struct v4l2_subdev_state *state)
{
	int ret;
	const struct ov16a1q_reg_list regs = {
		.num_of_regs = ARRAY_SIZE(ov16a1q_regs),
		.regs = ov16a1q_regs,
	};

	ret = ov16a1q_write_reg_list(ov16a1q, &regs);
	if (ret)
		return ret;

	ret = ov16a1q_write_reg_list(ov16a1q, &ov16a1q->cur_mode->reg_list);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_handler_setup(&ov16a1q->ctrl_handler);
	if (ret)
		return ret;

	ret = ov16a1q_write(ov16a1q, 0x0100, 1, 0x01);
	if (ret)
		return ret;

	return 0;
}

static int ov16a1q_stop_stream(struct ov16a1q *ov16a1q)
{
	int ret;

	ret = ov16a1q_write(ov16a1q, 0x0100, 1, 0x00);
	if (ret)
		return ret;

	return 0;
}

static int ov16a1q_s_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov16a1q *ov16a1q = to_ov16a1q(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (on) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto unlock_and_return;

		ret = ov16a1q_start_stream(ov16a1q, state);
		if (ret) {
			dev_err(&client->dev, "Failed to start streaming\n");
			pm_runtime_put_sync(&client->dev);
			goto unlock_and_return;
		}
	} else {
		ov16a1q_stop_stream(ov16a1q);
		pm_runtime_mark_last_busy(&client->dev);
		pm_runtime_put_autosuspend(&client->dev);
	}

unlock_and_return:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int ov16a1q_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	struct ov16a1q *ov16a1q = to_ov16a1q(sd);
	struct ov16a1q_mode *mode;
	u64 pixel_rate;
	u32 v_blank;
	u32 h_blank;

	mode = v4l2_find_nearest_size(ov16a1q_modes, ARRAY_SIZE(ov16a1q_modes),
					  width, height, fmt->format.width,
					  fmt->format.height);

	fmt->format.code = mode->mbus_code;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(state, 0) =  fmt->format;
	} else {
		ov16a1q->cur_mode = mode;
		pixel_rate = mode->link_freq * 2 * mode->lane_count / mode->depth;
		__v4l2_ctrl_s_ctrl_int64(ov16a1q->pixel_rate, pixel_rate);
		/* Update limits and set FPS to default */
		v_blank = mode->vts - mode->height;
		__v4l2_ctrl_modify_range(ov16a1q->vblank, v_blank,
					 0xffff - mode->height,
					 1, v_blank);
		__v4l2_ctrl_s_ctrl(ov16a1q->vblank, v_blank);
		h_blank = mode->hts - mode->width;
		__v4l2_ctrl_modify_range(ov16a1q->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	return 0;
}

static int ov16a1q_get_selection(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_selection *sel)
{
	struct ov16a1q *ov16a1q = to_ov16a1q(sd);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(sd_state, sel->pad);
		return 0;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = ov16a1q->cur_mode->width;
		sel->r.height = ov16a1q->cur_mode->height;
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = ov16a1q->cur_mode->width;
		sel->r.height = ov16a1q->cur_mode->height;
		return 0;
	}

	return -EINVAL;
}

static int ov16a1q_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(ov16a1q_modes))
		return -EINVAL;

	if (fse->code != ov16a1q_modes[fse->index].mbus_code)
		return -EINVAL;

	fse->min_width  = ov16a1q_modes[fse->index].width;
	fse->max_width  = ov16a1q_modes[fse->index].width;
	fse->max_height = ov16a1q_modes[fse->index].height;
	fse->min_height = ov16a1q_modes[fse->index].height;

	return 0;
}

static int ov16a1q_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct ov16a1q *ov16a1q = to_ov16a1q(sd);

	if (code->index != 0)
		return -EINVAL;

	code->code = ov16a1q->cur_mode->mbus_code;

	return 0;
}

static int ov16a1q_init_state(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state)
{
	struct ov16a1q *ov16a1q = to_ov16a1q(sd);
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.width = ov16a1q->cur_mode->width,
			.height = ov16a1q->cur_mode->height,
		},
	};

	ov16a1q_set_fmt(sd, sd_state, &fmt);

	return 0;
}

static int ov16a1q_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov16a1q *ov16a1q = container_of(ctrl->handler,
						 struct ov16a1q, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ov16a1q->sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	u32 exposure_max;
	int ret;

	state = v4l2_subdev_get_locked_active_state(&ov16a1q->sd);
	format = v4l2_subdev_state_get_format(state, 0);

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = ov16a1q->cur_mode->height + ctrl->val - 2;
		__v4l2_ctrl_modify_range(ov16a1q->exposure,
					 ov16a1q->exposure->minimum,
					 exposure_max, ov16a1q->exposure->step,
					 exposure_max);
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = ov16a1q_write(ov16a1q, 0x3500, 3, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov16a1q_write(ov16a1q, 0x3508, 2, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = ov16a1q_write(ov16a1q, 0x380e, 2, ov16a1q->cur_mode->height + ctrl->val);
		break;
	default:
		ret = -EINVAL;
		dev_warn(&client->dev, "%s Unhandled id: 0x%x\n",
			 __func__, ctrl->id);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_subdev_core_ops ov16a1q_core_ops = { };

static const struct v4l2_subdev_video_ops ov16a1q_video_ops = {
	.s_stream = ov16a1q_s_stream,
};

static const struct v4l2_subdev_pad_ops ov16a1q_pad_ops = {
	.enum_mbus_code = ov16a1q_enum_mbus_code,
	.enum_frame_size = ov16a1q_enum_frame_sizes,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ov16a1q_set_fmt,
	.get_selection = ov16a1q_get_selection,
};

static const struct v4l2_subdev_ops ov16a1q_subdev_ops = {
	.core	= &ov16a1q_core_ops,
	.video	= &ov16a1q_video_ops,
	.pad	= &ov16a1q_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov16a1q_internal_ops = {
	.init_state = ov16a1q_init_state,
};

static const struct v4l2_ctrl_ops ov16a1q_ctrl_ops = {
	.s_ctrl = ov16a1q_set_ctrl,
};

static int ov16a1q_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov16a1q *ov16a1q = to_ov16a1q(sd);
	int ret;

	gpiod_set_value_cansleep(ov16a1q->reset_gpio, 0);

	ret = clk_prepare_enable(ov16a1q->xvclk);
	if (ret) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	usleep_range(2000, 3000);

	ret = regulator_bulk_enable(ARRAY_SIZE(ov16a1q_supply_names),
					ov16a1q->supplies);
	if (ret) {
		dev_err(dev, "failed to enable regulators\n");
		goto disable_clk;
	}

	gpiod_set_value_cansleep(ov16a1q->reset_gpio, 1);
	usleep_range(1000, 2000);

	return 0;

disable_clk:
	clk_disable_unprepare(ov16a1q->xvclk);
	return ret;
};

static int ov16a1q_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov16a1q *ov16a1q = to_ov16a1q(sd);

	gpiod_set_value_cansleep(ov16a1q->reset_gpio, 0);
	usleep_range(2000, 3000);

	clk_disable_unprepare(ov16a1q->xvclk);
	usleep_range(2000, 3000);

	regulator_bulk_disable(ARRAY_SIZE(ov16a1q_supply_names),
				   ov16a1q->supplies);
	return 0;
};

static int ov16a1q_init_ctrls(struct ov16a1q *ov16a1q)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov16a1q->sd);
	struct v4l2_ctrl_handler *handler = &ov16a1q->ctrl_handler;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *ctrl;
	struct ov16a1q_mode *mode = ov16a1q->cur_mode;
	u64 pixel_rate;
	u32 h_blank;
	u32 v_blank;
	u32 exposure_max;
	int ret;
	static s64 link_freq[] = {
		0
	};
	link_freq[0] = mode->link_freq;

	ret = v4l2_ctrl_handler_init(handler, 5);
	if (ret)
		return ret;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
					  ARRAY_SIZE(link_freq) - 1, 0, link_freq);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = mode->link_freq * 2 * mode->lane_count / mode->depth;
	ov16a1q->pixel_rate = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, pixel_rate, 1, pixel_rate);

	h_blank = mode->hts - mode->width;
	ov16a1q->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (ov16a1q->hblank)
		ov16a1q->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v_blank = mode->vts - mode->height;
	ov16a1q->vblank = v4l2_ctrl_new_std(handler, &ov16a1q_ctrl_ops,
					   V4L2_CID_VBLANK, v_blank,
					   0x7ff7 - mode->height,
					   1, v_blank);

	exposure_max = mode->vts - 4;
	ov16a1q->exposure = v4l2_ctrl_new_std(handler, &ov16a1q_ctrl_ops,
						 V4L2_CID_EXPOSURE,
						 0,
						 exposure_max, 1,
						 exposure_max);

	v4l2_ctrl_new_std(handler, &ov16a1q_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  128, 1984, 1, 128);

	if (handler->error) {
		ret = handler->error;
		goto err_free_handler;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto err_free_handler;

	ret = v4l2_ctrl_new_fwnode_properties(handler, &ov16a1q_ctrl_ops,
						  &props);
	if (ret)
		goto err_free_handler;

	ov16a1q->sd.ctrl_handler = handler;

	return 0;

err_free_handler:
	dev_err(&client->dev, "Failed to init controls: %d\n", ret);
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov16a1q_check_sensor_id(struct ov16a1q *ov16a1q)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov16a1q->sd);
	u32 id = 0;
	int ret;

	ret = ov16a1q_read(ov16a1q, 0x300b, 2, &id);
	if (ret)
		return ret;

	if (id != 0x1641) {
		dev_err(&client->dev, "Chip ID mismatch: expected 0x%x, got 0x%x\n", 0x1641, id);
		return -ENODEV;
	}

	dev_info(&client->dev, "Detected ov16a1q sensor\n");
	return 0;
}

static int ov16a1q_parse_of(struct ov16a1q *ov16a1q)
{
	struct v4l2_fwnode_endpoint vep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct i2c_client *client = v4l2_get_subdevdata(&ov16a1q->sd);
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &vep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "Failed to parse endpoint: %d\n", ret);
		return ret;
	}

	for (unsigned int i = 0; i < ARRAY_SIZE(ov16a1q_modes); i++) {
		struct ov16a1q_mode *mode = &ov16a1q_modes[i];

		if (mode->lane_count != vep.bus.mipi_csi2.num_data_lanes)
			continue;

		ov16a1q->cur_mode = mode;
		break;
	}

	if (!ov16a1q->cur_mode) {
		dev_err(dev, "Unsupported number of data lanes %u\n",
			vep.bus.mipi_csi2.num_data_lanes);
		return -EINVAL;
	}

	return 0;
}

static int ov16a1q_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov16a1q *ov16a1q;
	struct v4l2_subdev *sd;
	int ret;

	ov16a1q = devm_kzalloc(dev, sizeof(*ov16a1q), GFP_KERNEL);
	if (!ov16a1q)
		return -ENOMEM;

	ov16a1q->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov16a1q->xvclk))
		return dev_err_probe(dev, PTR_ERR(ov16a1q->xvclk),
					 "Failed to get xvclk\n");

	ov16a1q->reset_gpio = devm_gpiod_get(dev, "reset",
							 GPIOD_OUT_LOW);
	if (IS_ERR(ov16a1q->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ov16a1q->reset_gpio),
					 "Failed to get reset gpio\n");

	v4l2_i2c_subdev_init(&ov16a1q->sd, client, &ov16a1q_subdev_ops);
	ov16a1q->sd.internal_ops = &ov16a1q_internal_ops;

	for (unsigned int i = 0; i < ARRAY_SIZE(ov16a1q_supply_names); i++)
		ov16a1q->supplies[i].supply = ov16a1q_supply_names[i];

	ret = devm_regulator_bulk_get(&client->dev,
					   ARRAY_SIZE(ov16a1q_supply_names),
					   ov16a1q->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ret = ov16a1q_parse_of(ov16a1q);
	if (ret)
		return ret;

	ret = ov16a1q_init_ctrls(ov16a1q);
	if (ret)
		return ret;

	sd = &ov16a1q->sd;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov16a1q->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov16a1q->pad);
	if (ret < 0)
		goto err_free_handler;

	sd->state_lock = ov16a1q->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(sd);
	if (ret < 0) {
		dev_err(&client->dev, "Subdev initialization error %d\n", ret);
		goto err_clean_entity;
	}

	ret = ov16a1q_power_on(dev);
	if (ret)
		goto err_clean_entity;

	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);

	ret = ov16a1q_check_sensor_id(ov16a1q);
	if (ret)
		goto err_power_off;

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	ret = v4l2_async_register_subdev_sensor(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_power_off;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_power_off:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	ov16a1q_power_off(dev);
err_clean_entity:
	media_entity_cleanup(&sd->entity);
err_free_handler:
	v4l2_ctrl_handler_free(&ov16a1q->ctrl_handler);

	return ret;
};

static void ov16a1q_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov16a1q *ov16a1q = to_ov16a1q(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&ov16a1q->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ov16a1q_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct dev_pm_ops ov16a1q_pm_ops = {
	SET_RUNTIME_PM_OPS(ov16a1q_power_off, ov16a1q_power_on, NULL)
};

static const struct of_device_id ov16a1q_of_match[] = {
	{ .compatible = "ovti,ov16a1q" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ov16a1q_of_match);

static struct i2c_driver ov16a1q_i2c_driver = {
	.driver = {
		.of_match_table = ov16a1q_of_match,
		.pm = &ov16a1q_pm_ops,
		.name = "ov16a1q",
	},
	.probe  = ov16a1q_probe,
	.remove = ov16a1q_remove,
};

module_i2c_driver(ov16a1q_i2c_driver)

MODULE_DESCRIPTION("Omnivision OV16A1Q image sensor subdev driver");
MODULE_LICENSE("GPL");
