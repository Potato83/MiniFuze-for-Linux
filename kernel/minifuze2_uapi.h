/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef MINIFUZE2_UAPI_H
#define MINIFUZE2_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define MF2_ABI_VERSION 1
#define MF2_CAP_DIRECT_MONO (1u << 0)
#define MF2_CAP_PHANTOM48V (1u << 1)
#define MF2_CAP_ROUTING (1u << 2)
#define MF2_CAP_METERS (1u << 3)

struct mf2_meters {
	__u32 version;
	__u8 data[16];
};

struct mf2_switch {
	__u32 version;
	__u16 selector;
	__u16 value;
};

struct mf2_state {
	__u32 version;
	__u8 data[46];
	__u8 reserved[2];
};

struct mf2_info {
	__u32 version;
	__u16 vid;
	__u16 pid;
	__u16 bcd_device;
	__u16 capabilities;
	__u8 port[64];
};

#define MF2_GET_SWITCH _IOWR('F', 0x20, struct mf2_switch)
#define MF2_GET_STATE _IOR('F', 0x21, struct mf2_state)
#define MF2_SET_INST _IOWR('F', 0x22, struct mf2_switch)
#define MF2_GET_INFO _IOR('F', 0x23, struct mf2_info)
#define MF2_SET_DIRECT_MONO _IOWR('F', 0x24, struct mf2_switch)
#define MF2_SET_PHANTOM48V _IOWR('F', 0x25, struct mf2_switch)
#define MF2_SET_ROUTING _IOWR('F', 0x26, struct mf2_switch)
#define MF2_GET_METERS _IOR('F', 0x27, struct mf2_meters)

#endif
