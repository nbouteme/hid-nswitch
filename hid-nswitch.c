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

#include "hid-nswitch.h"

DEFINE_SPINLOCK(global_lock);
__u8 allocated_players[8];
LIST_HEAD(ljoycons);
LIST_HEAD(rjoycons);
LIST_HEAD(procontrollers);

/* Event Handler */
void simplejc_prepare(nswitch_dev *ndev);

/* Worker Thread */
void handshake_rumble(nswitch_dev *ndev) {
	nswitch_dev_input_report res;

	res = ns_exchange(ndev, &(output_command) {
			RUMBLE_REPORT, 0, 0, {
				0xc2, 0xc8, 0x03, 0x72,
				0xc2, 0xc8, 0x03, 0x72
			}, SET_VIBRATION, {}
	});

	res = ns_exchange(ndev, &(output_command) {
			RUMBLE_REPORT, 0, 0, {
				0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00
			}, SET_VIBRATION, {}
	});
/*
	res = ns_exchange(ndev, &(output_command) {
			RUMBLE_REPORT, 0, 0, {
				0xc2, 0xc8, 0x03, 0x72,
				0xc2, 0xc8, 0x03, 0x72
			}, SET_VIBRATION
	});

	res = ns_exchange(ndev, &(output_command) {
			RUMBLE_REPORT, 0, 0, {
				0xc2, 0xc8, 0x60, 0x64,
				0xc2, 0xc8, 0x60, 0x64
			}, SET_VIBRATION
	});
*/
}

static struct list_head *select_list(enum nswitch_dev_type type) {
	switch(type) {
	case LEFT_JOYCON:
		return &ljoycons;
	case RIGHT_JOYCON:
		return &rjoycons;
	case PRO_CONTROLLER:
		return &procontrollers;
	default:
		break;
	}
	return 0;
}

void dump_mem(struct hid_device *hdev, __u8 *s, int size);

static ssize_t nswitch_dev_show(struct device *dev,
								struct device_attribute *attr,
								char *buf)
{
	return sprintf(buf, "something\n");
}

/*
  Requires cmd_lock
 */
/* TODO: rewrite the mecanism to work from any thread */
int nd_send_cmd(nswitch_dev *ndev, output_command *oc) {
	oc->gpn = ++ndev->cmdcounter;
	if (oc->report == BASIC) {
		ndev->reply = oc->subcommand;
		hid_info(ndev->hdev, "Sending command %02x", oc->subcommand);
		reinit_completion(&ndev->cmd_pending);
	}
	hid_hw_output_report(ndev->hdev, (void*)oc, sizeof(*oc));
	return 1;
}

/* Worker Thread */
int nd_wait(nswitch_dev *jdev, struct completion *smth) {
	int ret;

	ret = wait_for_completion_interruptible(smth);
	if (ret < 0)
		return -ERESTARTSYS;
	return ret;
}

/* Worker Thread */
int nd_wait_reply(nswitch_dev *jdev) {
	if (!jdev->reply) {
		hid_err(jdev->hdev, "No reply awaiting\n");
		return 0;
	}
	return nd_wait(jdev, &jdev->cmd_pending);
}

/* Worker Thread */
static void init_calibration_data(nswitch_dev *ndev) {
	nswitch_dev_input_report res;
	left_stick_calibration_data *lscd;
	right_stick_calibration_data *rscd;
	sax_calibration_data *scd;
	spi_read_reply *srr;
	__u32 *coeff;

	/* TODO: Refactor */
	srr = (void*)&res.full.reply.data;
	res = ns_exchange(ndev, &(output_command) {
			BASIC, 0, 0, {}, SPI_FLASH_READ, {
				.spi_read = USER_CALIBRATION_LEFT_STICK
			}
	});

	if (srr->data[0] != 0xB2 || srr->data[1] != 0xA1) {
		hid_info(ndev->hdev, "No LS user config, loading factory settings...");
		res = ns_exchange(ndev, &(output_command) {
				BASIC, 0, 0, {}, SPI_FLASH_READ, {
					.spi_read = FACTORY_CALIBRATION_LEFT_STICK
				}
			});
		lscd = (void*)srr->data;
	} else
		lscd = (void*)(srr->data + 2);

	ndev->calibration.left_stick = *lscd;

	res = ns_exchange(ndev, &(output_command) {
			BASIC, 0, 0, {}, SPI_FLASH_READ, {
				.spi_read = USER_CALIBRATION_RIGHT_STICK
			}
	});

	if (srr->data[0] != 0xB2 || srr->data[1] != 0xA1) {
		hid_info(ndev->hdev, "No RS user config, loading factory settings...");
		res = ns_exchange(ndev, &(output_command) {
				BASIC, 0, 0, {}, SPI_FLASH_READ, {
					.spi_read = FACTORY_CALIBRATION_RIGHT_STICK
				}
			});
		rscd = (void*)srr->data;
	} else
		rscd = (void*)(srr->data + 2);
	ndev->calibration.right_stick = *rscd;

	res = ns_exchange(ndev, &(output_command) {
			BASIC, 0, 0, {}, SPI_FLASH_READ, {
				.spi_read = USER_CALIBRATION_6AXIS
			}
	});

	if (srr->data[0] != 0xB2 || srr->data[1] != 0xA1) {
		hid_info(ndev->hdev, "No 6AXIS user config, loading factory settings...");
		res = ns_exchange(ndev, &(output_command) {
				BASIC, 0, 0, {}, SPI_FLASH_READ, {
					.spi_read = FACTORY_CALIBRATION_6AXIS
				}
			});
		scd = (void*)srr->data;
	} else
		scd = (void*)(srr->data + 2);

	ndev->calibration.sax = *scd;

	coeff = ndev->calibration.gyro_coeff;
	coeff[0] = 816 / (13371 - (scd->gyroscope_origin[0]));
	coeff[1] = 816 / (13371 - (scd->gyroscope_origin[1]));
	coeff[2] = 816 / (13371 - (scd->gyroscope_origin[2]));
}

/* Event Handler */
static void prepare_projoypad(nswitch_dev *ndev) {
	calibration_data *lcd, *rcd;
	unsigned int i;

	lcd = &ndev->calibration;
	rcd = &ndev->calibration;
	ndev->siminput = input_allocate_device();
	input_set_drvdata(ndev->siminput, ndev);

	ndev->siminput->dev.parent = &ndev->hdev->dev;
	ndev->siminput->id.bustype = ndev->hdev->bus;
	ndev->siminput->id.vendor = ndev->hdev->vendor;
	ndev->siminput->id.product = ndev->hdev->product;
	ndev->siminput->id.version = ndev->hdev->version;
	ndev->siminput->name = kasprintf(GFP_KERNEL, "%s Dual Joycon Controller" , ndev->hdev->name);

	set_bit(EV_KEY, ndev->siminput->evbit);
	for (i = 0; i < ARRAY_SIZE(ns_buttons); ++i)
		set_bit(ns_buttons[i], ndev->siminput->keybit);

	set_bit(EV_ABS, ndev->siminput->evbit);

	set_bit(ABS_X, ndev->siminput->absbit);
	set_bit(ABS_Y, ndev->siminput->absbit);
	set_bit(ABS_RX, ndev->siminput->absbit);
	set_bit(ABS_RY, ndev->siminput->absbit);

	input_set_abs_params(ndev->siminput, ABS_X,
						 lcd->left_stick.xcenter - lcd->left_stick.xmin_offset,
						 lcd->left_stick.xcenter + lcd->left_stick.xmax_offset, 10, 0);
	input_set_abs_params(ndev->siminput, ABS_Y,
						 lcd->left_stick.ycenter - lcd->left_stick.ymin_offset,
						 lcd->left_stick.ycenter + lcd->left_stick.ymax_offset, 10, 0);
	input_set_abs_params(ndev->siminput, ABS_RX,
						 rcd->right_stick.xcenter - rcd->right_stick.xmin_offset,
						 rcd->right_stick.xcenter + rcd->right_stick.xmax_offset, 10, 0);
	input_set_abs_params(ndev->siminput, ABS_RY,
						 rcd->right_stick.ycenter - rcd->right_stick.ymin_offset,
						 rcd->right_stick.ycenter + rcd->right_stick.ymax_offset, 10, 0);

	//ndev->handler = report_dual_keys;

	hid_info(ndev->hdev, "Handler set to report keys...");
	input_register_device(ndev->siminput);
	ns_exchange(ndev, &(output_command) {
			BASIC, 0, 0, {}, SET_INPUT_REPORT_MODE, {
				.mode = STANDARD
	}});
}

/* Event Handler */
static void projc_prepare(nswitch_dev *ndev) {
	if (ndev->ledcache >> 4 != 0xF) {
		set_leds(ndev, 0xF0);
	}
	if (ndev->state.simple.right) {
		/* TODO: Implement this */
		hid_info(ndev->hdev, "ProJC validated");
		prepare_projoypad(ndev);
	} else if (ndev->state.simple.down) {
		hid_info(ndev->hdev, "ProJC unvalidated");
		ndev->handler = projc_prepare;
	}
}

/*
  Should only be called from a worker thread
 */
/* Worker Thread */
nswitch_dev_input_report ns_exchange(nswitch_dev *ndev,
									 output_command *oc) {
	unsigned long flags;
	nswitch_dev_input_report ret = {0};
	int r;

	spin_lock_irqsave(&ndev->cmd_lock, flags);
	if (ndev->reply) {
		hid_err(ndev->hdev, "Command %02x pending, waiting for it to finish", ndev->reply);
		spin_unlock_irqrestore(&ndev->cmd_lock, flags);
		r = wait_for_completion_interruptible_timeout(&ndev->cmd_pending, HZ);
		spin_lock_irqsave(&ndev->cmd_lock, flags);
		if (r <= 0 || ndev->reply) {
			hid_err(ndev->hdev, "Something's gone horribly wrong %d %02x\n", r, ndev->reply);
			goto end;
		}
	}
	nd_send_cmd(ndev, oc);
	spin_unlock_irqrestore(&ndev->cmd_lock, flags);
	if (ndev->deinit) {
		hid_err(ndev->hdev, "Device is deiniting\n");
		goto end;
	}
	if(oc->report == BASIC) {
		nd_wait_reply(ndev);
		spin_lock_irqsave(&ndev->state_lock, flags);
		memcpy(&ret, &ndev->state, sizeof(ret));
		spin_unlock_irqrestore(&ndev->state_lock, flags);
	}
end:
	return ret;
}

/* Worker Thread */
static void init_rumble(nswitch_dev *ndev) {
	ns_exchange(ndev, &(output_command) {
			BASIC, 0, 0, {}, SET_VIBRATION, {
				.vibrate = 1
			}
	});
}

static void init_ir_cam(nswitch_dev *ndev) {
	/* TODO:  */
}

static void init_home_led(nswitch_dev *ndev) {
	/* TODO:  */
}

/*
  Put the device in a simple state.
  Get info on the device and initialize the needed devices in sysfs
 */
/* Worker Thread */
void nswitch_dev_init_worker(struct work_struct *work)
{
	nswitch_dev *ndev = container_of(work,
									 nswitch_dev,
									 init_worker);
	nswitch_dev_input_report res;
	nswitch_devinfo *info = (void*)&res.full.reply.data;
	unsigned long flags;
	struct list_head *target;
	nswitch_list *nl;

	res = ns_exchange(ndev, &(output_command) {
			BASIC, 0, 0, {}, DEVICE_INFO, {}
	});

	hid_info(ndev->hdev, "Type: ID: %d\n", info->type);
	hid_info(ndev->hdev, "New %s. v%d.%d. (%02x:%02x:%02x:%02x:%02x:%02x)",
			 ndev->hdev->name,
			 info->vmajor, info->vminor,
			 info->mac[0], info->mac[1], info->mac[2],
			 info->mac[3], info->mac[4], info->mac[5]);

	ndev->info = *info;
	hid_info(ndev->hdev, "Allocated at: %p\n", ndev);
	dump_mem(ndev->hdev, (void*)&ndev->info, sizeof(*info));

	init_player_leds(ndev);
	init_battery(ndev);
	ndev->inited_hw = 1;
	//init_keys(ndev);
//init_axis(ndev);
	init_rumble(ndev);
	init_calibration_data(ndev);

	// TODO:  exposes rom/ram/spi into char devices
	//init_memory_map(ndev);

	handshake_rumble(ndev);

	switch(info->type) {
	case RIGHT_JOYCON:
		init_ir_cam(ndev);
		/* fallthrough */
	case PRO_CONTROLLER:
		init_home_led(ndev);
		/* fallthrough */
	case LEFT_JOYCON:
		ndev->handler = ((update_fun_t[]){
			&simplejc_prepare,
			&simplejc_prepare,
			&projc_prepare
		})[info->type - LEFT_JOYCON];
		break;
	default:
		hid_info(ndev->hdev, "Unknown device type %d\n", info->type);
		return;
	}

	nl = kzalloc(sizeof(*nl), GFP_KERNEL);
	INIT_LIST_HEAD(&nl->list);
	nl->ndev = ndev;
	target = select_list(info->type);
	spin_lock_irqsave(&global_lock, flags);
	list_add(target, &nl->list);
	spin_unlock_irqrestore(&global_lock, flags);
	while (nd_wait(ndev, &ndev->state_pending) >= 0 && !ndev->deinit) {
		reinit_completion(&ndev->state_pending);
		ndev->handler(ndev);
	}
}

static DEVICE_ATTR(devtype, S_IRUGO, nswitch_dev_show, NULL);

/* Event Handler */
static nswitch_dev *nswitch_dev_create(struct hid_device *hdev,
									   const struct hid_device_id *id)
{
	nswitch_dev *nsd;

	nsd = kzalloc(sizeof(*nsd), GFP_KERNEL);
	if (!nsd)
		return NULL;

	memset(nsd, 0, sizeof(*nsd));
	nsd->hdev = hdev;
	hid_set_drvdata(hdev, nsd);

	spin_lock_init(&nsd->state_lock);
	spin_lock_init(&nsd->cmd_lock);
	init_completion(&nsd->cmd_pending);
	init_completion(&nsd->state_pending);

	INIT_WORK(&nsd->init_worker, nswitch_dev_init_worker);
	//INIT_WORK(&nsd->cmd_worker, nswitch_dev_cmd_worker);
	schedule_work(&nsd->init_worker);
	return nsd;
}

/* Event Handler */
static int nswitch_hid_probe(struct hid_device *hdev,
							 const struct hid_device_id *id)
{
	int ret;
	nswitch_dev *nsdev;

	nsdev = nswitch_dev_create(hdev, id);
	if (!nsdev) {
		hid_err(hdev, "Can't alloc device\n");
		return -ENOMEM;
	}

	hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "HID parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "HW start failed\n");
		goto err;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "cannot start hardware I/O\n");
		goto err_stop;
	}

	ret = device_create_file(&hdev->dev, &dev_attr_devtype);
	if (ret) {
		hid_err(hdev, "cannot create sysfs attribute\n");
		goto err_close;
	}

	hid_info(hdev, "New device registered\n");
	return 0;

err_close:
	hid_hw_close(hdev);
err_stop:
	hid_hw_stop(hdev);
err:
	kfree(nsdev);
	return ret;
}

void dump_mem(struct hid_device *hdev, __u8 *s, int size) {
	int i = 0;
	__u8 buf[8];

	while (size > 0) {
		memset(buf, 0xFE, sizeof(buf));
		memcpy(buf, s + i, size >= 8 ? 8 : size);
		hid_warn(hdev, "%d: %02x%02x %02x%02x %02x%02x %02x%02x",
				 i,
				 buf[0], buf[1], buf[2], buf[3],
				 buf[4], buf[5], buf[6], buf[7]
			);
		size -= 8;
		i += 8;
	}
}

/* Event Handler */
static int nswitch_hid_event(struct hid_device *hdev,
							 struct hid_report *report,
							 u8 *raw_data,
							 int size) {
	nswitch_dev *nsdev = hid_get_drvdata(hdev);
	nswitch_dev_input_report *rep;
	unsigned long flags;

	if ((unsigned)size > sizeof(*rep)) {
		hid_warn(hdev, "Input report too big");
		return 1;
	}
	rep = (void*) raw_data;
	spin_lock_irqsave(&nsdev->state_lock, flags);
	nsdev->state.input_report = rep->input_report;
	spin_unlock_irqrestore(&nsdev->state_lock, flags);

	switch (rep->input_report) {
	case REPLY:
		dump_mem(hdev, raw_data, size);
		if (nsdev->reply != rep->full.reply.reply_to) {
			hid_warn(nsdev->hdev, "Got a reply for an unsollicited command");
			hid_warn(nsdev->hdev, "Expected %02x, got %02x", nsdev->reply, rep->full.reply.reply_to);
			return 1;
		} else {
			hid_info(nsdev->hdev, "Reply %02x received on event handler\n", nsdev->reply);
		}
		spin_lock_irqsave(&nsdev->cmd_lock, flags);
		nsdev->reply = 0;
		spin_unlock_irqrestore(&nsdev->cmd_lock, flags);

		spin_lock_irqsave(&nsdev->state_lock, flags);
		nsdev->reply_data = rep->full.reply;
		spin_unlock_irqrestore(&nsdev->state_lock, flags);
		complete_all(&nsdev->cmd_pending);
/* fall through */
	case STANDARD:
	case STD_NFCIR:
	case STD_UNKNOWN0:
	case STD_UNKNOWN1:
		spin_lock_irqsave(&nsdev->state_lock, flags);
		nsdev->state.full = rep->full;
		spin_unlock_irqrestore(&nsdev->state_lock, flags);
		break;
	case SIMPLE:
		spin_lock_irqsave(&nsdev->state_lock, flags);
		nsdev->state.simple = rep->simple;
		spin_unlock_irqrestore(&nsdev->state_lock, flags);
		break;
	default:
		hid_warn(hdev, "Unhandled input report type %02x", rep->input_report);
	}

	complete_all(&nsdev->state_pending);
	return 0;
}

/* Event Handler */
static void nswitch_hid_remove(struct hid_device *hdev) {
	int i;
	nswitch_dev *ndev = hid_get_drvdata(hdev);

	ndev->deinit = 1;
	complete_all(&ndev->state_pending);
	hid_info(hdev, "remove requested");
	cancel_work_sync(&ndev->init_worker);
	//cancel_work_sync(&ndev->cmd_worker);

	if (ndev->inited_hw) {
		for (i = 0; i < 4; ++i)
			led_classdev_unregister(ndev->player_leds + i);

		power_supply_unregister(ndev->battery);
		kfree(ndev->battery_desc.name);

		//kfree(ndev->siminput->name);
		//ndev->siminput->name = 0;
		if (ndev->siminput) {
			input_unregister_device(ndev->siminput);
		}

		if (ndev->right) {
			if (ndev->info.type == LEFT_JOYCON) {
				ndev->right->handler = &simplejc_prepare;
				ns_exchange(ndev->right, &(output_command) {
						BASIC, 0, 0, {}, SET_INPUT_REPORT_MODE, {
							.mode = SIMPLE
				}});
				ndev->right->right = 0;
				ndev->right = 0;
			}
		}
	}

	hid_info(hdev, "finished disabling hardware");
	device_remove_file(&hdev->dev, &dev_attr_devtype);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	kfree(ndev);
}

static const struct hid_device_id nswitch_hid_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
				USB_DEVICE_ID_NINTENDO_JOYCON_L) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
				USB_DEVICE_ID_NINTENDO_JOYCON_R) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
				USB_DEVICE_ID_NINTENDO_NS_PRO_CONTROLLER) },
/*	{ HID_USB_DEVICE(USB_VENDOR_ID_NINTENDO,
	USB_DEVICE_ID_NINTENDO_NS_PRO_CONTROLLER) },*/
	{ }
};
MODULE_DEVICE_TABLE(hid, nswitch_hid_devices);

static struct hid_driver nswitch_hid_driver = {
	.name = "nswitch",
	.id_table = nswitch_hid_devices,
	.probe = nswitch_hid_probe,
	.remove = nswitch_hid_remove,
	.raw_event = nswitch_hid_event
};
static int __init nswitch_hid_driver_init(void)
{
	/* TODO: init RPC file */
	return hid_register_driver(&nswitch_hid_driver);
}
static void __exit nswitch_hid_driver_exit(void)
{
	hid_unregister_driver(&nswitch_hid_driver);
}
module_init(nswitch_hid_driver_init);
module_exit(nswitch_hid_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nabil Boutemeur <nabil.boutemeur@gmail.com>");
MODULE_DESCRIPTION("Driver for Nintendo Switch peripherals");
