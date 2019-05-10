#ifndef __HID_NSWITCH_H
#define __HID_NSWITCH_H

/*
 * HID driver for Nintendo Switch peripherals
 * Copyright (c) 2018 Nabil Boutemeur <nabil.boutemeur@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/list.h>

#define USB_VENDOR_ID_NINTENDO 0x057e
#define USB_DEVICE_ID_NINTENDO_JOYCON_L	0x2006
#define USB_DEVICE_ID_NINTENDO_JOYCON_R	0x2007
#define USB_DEVICE_ID_NINTENDO_NS_PRO_CONTROLLER	0x2009
/* TODO: Handle Charging Grip */

/*
From:
How is the order of the controllers indicated by the player LEDs?
https://en-americas-support.nintendo.com/app/answers/detail/a_id/22424/~/controller-pairing-faq
That's what it looks like with the sync button on the right

That's 8 players with at most 16 simultaneous devices.
I don't have this many joycons (and bluetooth adapters) to implement
limits so any additional player will have their LEDs off instead of being rejected.
*/
#define PLAYER_LEDS 0x6BA9FEC8

#define PACKED __attribute__((packed))

/*
  HCI states exposed by all the peripherals in this family
  through the Bluetooth HID in subcommand 0xXX
 */
enum nswitch_dev_hci_state {
	DISCONNECT,
	REBOOT_RECONNECT,
	REBOOT_PAIR,
	REBOOT_RECONNECT_HOME
};

typedef struct {
	__u16 xmax_offset	: 12;
	__u16 ymax_offset	: 12;
	__u16 xcenter		: 12;
	__u16 ycenter		: 12;
	__u16 xmin_offset	: 12;
	__u16 ymin_offset	: 12;
} PACKED left_stick_calibration_data;

typedef struct {
	__u16 xcenter		: 12;
	__u16 ycenter		: 12;
	__u16 xmin_offset	: 12;
	__u16 ymin_offset	: 12;
	__u16 xmax_offset	: 12;
	__u16 ymax_offset	: 12;
} PACKED right_stick_calibration_data;

typedef struct {
	__u16 accelerometer_origin[3];
	__u16 accelerometer_sensitivity[3];
	__u16 gyroscope_origin[3];
	__u16 gyroscope_sensitivity[3];
} PACKED sax_calibration_data;

typedef struct {
	left_stick_calibration_data left_stick;
	right_stick_calibration_data right_stick;
	sax_calibration_data sax;
	__u32 gyro_coeff[3];
} PACKED calibration_data;

/*
   Available stick directions (left joystick for the procontroller)
   when holding the controller sideway with SR/SL on the top
   when using 0x3F input reports.
 */
enum simple_stick_direction {
	TOP,
	TOP_RIGHT,
	RIGHT,
	BOTTOM_RIGHT,
	BOTTOM,
	BOTTOM_LEFT,
	LEFT,
	TOP_LEFT,
	NEUTRAL
};

/*
  HID input report types
 */
enum input_report_type {
	/*
	   Driver init peripherals to this type

	*/
	SIMPLE			= 0x3F,

	REPLY			= 0x21,
	STANDARD		= 0x30,
	STD_NFCIR		= 0x31,
	STD_UNKNOWN0	= 0x32,
	STD_UNKNOWN1	= 0x33,

	NFC_UPDATE		= 0x23
};

/*
  State as reported by input report 0x3F
 */
typedef struct {
	__u8  down		: 1;
	__u8  right		: 1;
	__u8  left		: 1;
	__u8  up		: 1;
	__u8  sl		: 1;
	__u8  sr		: 1;
	__u8  __res1	: 2;
	__u8  minus		: 1;
	__u8  plus		: 1;
	__u8  ls		: 1;
	__u8  rs		: 1;
	__u8  home		: 1;
	__u8  capture	: 1;
	__u8  lr		: 1; /* L or R on the corresponding joycon */
	__u8  z			: 1; /* ZL or ZR -- --- ------------- ------*/

	enum  simple_stick_direction direction : 8;

	__u64 padding;
} PACKED nswitch_dev_simple_state;

/*
  State reported by the standard input report types
*/
typedef struct {
	__u8 y		: 1;
	__u8 x		: 1;
	__u8 b		: 1;
	__u8 a		: 1;
	__u8 rsr	: 1;
	__u8 rsl	: 1;
	__u8 r		: 1;
	__u8 zr		: 1;

	__u8 minus			: 1;
	__u8 plus			: 1;
	__u8 rs				: 1;
	__u8 ls				: 1;
	__u8 home			: 1;
	__u8 capture		: 1;
	__u8 __res1			: 1;
	__u8 charging_grip	: 1;

	__u8 down	: 1;
	__u8 up		: 1;
	__u8 right	: 1;
	__u8 left	: 1;
	__u8 lsr	: 1;
	__u8 lsl	: 1;
	__u8 l		: 1;
	__u8 zl		: 1;
} PACKED standard_button_state;

/*
  Only when the input report type is REPLY
 */
typedef struct {
	__u8 ack;
	__u8 reply_to;
	__u8 data[35];
} PACKED subcmd_input;

typedef struct {
	__u16 x : 12;
	__u16 y : 12;
} PACKED stick_state;

typedef struct {
	__u8  timer;
	__u8  connection: 4;
	__u8  battery: 4;
	standard_button_state buttons;
	stick_state left_stick;
	stick_state right_stick;
	__u8 vib;
	union {
		subcmd_input reply;
		__u8 nfc_ir_mcu[37];
		__u16 ax6[3][2][3];
	};
} PACKED nswitch_dev_full_report;

enum nswitch_dev_type {
	LEFT_JOYCON = 1,
	RIGHT_JOYCON,
	PRO_CONTROLLER
};

/*
   Reply from subcmd 0x02
 */
typedef struct {
	__u8 vmajor;
	__u8 vminor;
	enum nswitch_dev_type type : 8;
	__u8 two;
	__u8 mac[6];
	__u8 one;
	__u8 colored;
} PACKED nswitch_devinfo;

typedef struct {
	enum input_report_type input_report : 8;
	union {
		nswitch_dev_simple_state simple;
		nswitch_dev_full_report full;
	};
} PACKED nswitch_dev_input_report;

enum report_type {
	BASIC				= 0x1,
	NFC_UPDATE_REPORT	= 0x3,
	RUMBLE_REPORT		= 0x10,
	UNKNOWN_REPORT		= 0x12
};

typedef struct {
	__u8 pair_type;
	__u8 host_bd_addr[6];
} PACKED manual_pair_args;

typedef struct {
	__u16 l, r, zl, zr, sl, sr, home;
} PACKED elapsed_trigger_time;

typedef struct {
	__u32 addr;
	__u8 size;
} PACKED spi_read_args_t;

typedef struct {
	spi_read_args_t echo;
	__u8 data[];
} PACKED spi_read_reply;

/* +2 for magic */
#define USER_CALIBRATION_LEFT_STICK {0x8010, 9 + 2}
#define USER_CALIBRATION_RIGHT_STICK {0x801B, 9 + 2}
#define USER_CALIBRATION_6AXIS {0x8028, 0x18 + 2}

#define FACTORY_CALIBRATION_LEFT_STICK {0x603D, 9}
#define FACTORY_CALIBRATION_RIGHT_STICK {0x6046, 9}
#define FACTORY_CALIBRATION_6AXIS {0x6020, 0x18}

enum subcommand_type {
	GET_CONTROLLER_STATE	= 0x00,
	MANUAL_PAIR				= 0x01,
	DEVICE_INFO				= 0x02,
	SET_INPUT_REPORT_MODE	= 0x03,
	ELAPSED_TRIGGER_TIME	= 0x04,
	PAGE_LIST_STATE			= 0x05,
	SET_HCI					= 0x06,
	RESET_PAIRING			= 0x07,
	SET_SHIPMENT			= 0x08,
	SPI_FLASH_READ			= 0x10,
	SPI_FLASH_WRITE			= 0x11,
	SPI_FLASH_ERASE_SECTOR	= 0x12,
	RESET_NFC_IR			= 0x20,
	SET_NFC_IR				= 0x21,
	SET_PLAYER_LIGHTS		= 0x30,
	GET_PLAYER_LIGHTS		= 0x31,
	SET_HOME_LIGHT			= 0x38,
	SET_IMU					= 0x40,
	SET_IMU_SENSITIVITY		= 0x41,
	SET_VIBRATION			= 0x48,
	GET_VOLTAGE				= 0x50,
};

typedef struct {
	enum report_type report : 8;
	__u8 gpn : 4;
	__u8 zero : 4;
	__u8 rumble_data[8];
	enum subcommand_type subcommand : 8;
	union {
		__u8 raw[54];
		manual_pair_args manual_pair;
		enum input_report_type mode : 8;
		elapsed_trigger_time ett;
		enum nswitch_dev_hci_state new_hci_state : 8;
		__u8 shipment_mode;
		spi_read_args_t spi_read;
		__u8 player_lights;
		__u8 imu_state;
		__u8 vibrate;
	};
} PACKED output_command;

typedef struct emulated_input emulated_input;

struct nswitch_dev;
typedef struct nswitch_dev nswitch_dev;

typedef void (*update_fun_t)(nswitch_dev *d);

struct nswitch_dev {
	struct spinlock cmd_lock;
	struct spinlock state_lock;

	struct hid_device *hdev;
	struct input_dev *siminput;
	struct input_dev *axis;
	struct power_supply *battery;
	struct power_supply_desc battery_desc;

	update_fun_t handler;

	struct led_classdev player_leds[4];
	struct led_classdev home_led;
	struct work_struct init_worker;
	struct work_struct cmd_worker;
	struct completion cmd_pending;
	struct completion state_pending;

	calibration_data calibration;
	nswitch_devinfo info;
	nswitch_dev_input_report state;
	subcmd_input reply_data;
	__u8 ledcache;
	__u8 inited_hw;
	__u8 deinit;

	__u8 reply;
	__u8 cmdcounter : 4;

	nswitch_dev *right;
};

typedef struct {
	struct list_head list;
	nswitch_dev *ndev;
} nswitch_list;

void init_keys(nswitch_dev *ndev);
int init_battery(nswitch_dev *ndev);
int init_player_leds(nswitch_dev *ndev);
int nd_send_cmd(nswitch_dev *ndev, output_command *oc);
int nd_wait_reply(nswitch_dev *jdev);
nswitch_dev_input_report ns_exchange(nswitch_dev *ndev,
									 output_command *oc);
void set_leds(nswitch_dev *ndev, __u8 mask);
void dump_mem(struct hid_device *hdev, __u8 *s, int size);
void handshake_rumble(nswitch_dev *ndev);
void simplejc_prepare(nswitch_dev *ndev);

extern spinlock_t global_lock;
extern __u8 allocated_players[8];
extern struct list_head ljoycons;
extern struct list_head rjoycons;
extern struct list_head procontrollers;

extern const short ns_simple_buttons[11];
extern const short ns_buttons[24];

#endif
