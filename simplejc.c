#include "hid-nswitch.h"

/*
  16, 17, 18, 19, 8, 21, 13, 20, 22, 23, 11, 5 bits per index
 */
const __u64 ljc_bit_offsets = 0x2EF6A36A89CA30;

/*
  3, 0, 1, 2, 12, 5, 9, 4, 6, 7, 10, 5 bits per index
 */
const __u64 rjc_bit_offsets = 0x28E6224AC10403;

const short ns_simple_buttons[] = {
	BTN_A, BTN_X, BTN_B, BTN_Y,
	BTN_TL, BTN_TL2, /* (-/Home)/SL */
	BTN_TR, BTN_TR2, /* (+/Capture)/SR */
	BTN_0, BTN_1, /* R/ZR or L/ZL */
	BTN_THUMB /* Analog stick button */
};

const short ns_buttons[] = {
	BTN_Y, BTN_X, BTN_B, BTN_A,
	BTN_0, BTN_1, /* Right SL/SR buttons */
	BTN_TR, BTN_TR2, /* R/ZR */
	BTN_2, BTN_3, /* Plus/Minus */
	BTN_THUMBR, BTN_THUMBL,
	BTN_4, BTN_5, /* Home/Capture */
	BTN_6, BTN_7, /* Reserved/Charging Grip*/
	KEY_DOWN, KEY_UP, KEY_RIGHT, KEY_LEFT,
	BTN_6, BTN_7, /* Left SL/SR buttons */
	BTN_TL, BTN_TL2, /* L/ZL */
};

static void prepare_dual_joypad(nswitch_dev *ndev);

/* Event Handler */
static void report_simple_keys(nswitch_dev *ndev) {
	struct input_dev *siminput = ndev->siminput;
	__u64 offsets = 0;
	nswitch_dev_full_report *fr;
	int idx;
	const short *ev = ns_simple_buttons;
	stick_state *ss;
	__u8 i;
	calibration_data *cd;

	union {
		__u32 rbuttons;
		standard_button_state sbs;
	} buttons;

	switch (ndev->state.input_report) {
	case REPLY:
	case SIMPLE:
	case NFC_UPDATE:
		hid_info(ndev->hdev, "I don't want this...");
		return;
	default:
		break;
	}

	fr = &ndev->state.full;
	buttons.rbuttons = 0;
	buttons.sbs = fr->buttons;
	cd = &ndev->calibration;
	switch (ndev->info.type) {
	case LEFT_JOYCON:
		offsets = ljc_bit_offsets;
		ss = &fr->left_stick;
		break;
	case RIGHT_JOYCON:
		offsets = rjc_bit_offsets;
		ss = &fr->right_stick;
		break;
	default:
		return;
	}
	i = 0;
	while (i < 11) {
		idx = (offsets >> (i * 5)) & 0x1F;
		input_report_key(siminput, *ev++, (buttons.rbuttons >> idx) & 1);
		++i;
	}

	input_report_abs(siminput, ABS_X, ss->x);
	input_report_abs(siminput, ABS_Y, ss->y);
	input_sync(siminput);
}

/* Event Handler */
static void report_simple_mouse(nswitch_dev *ndev) {
	struct input_dev *siminput = ndev->siminput;
	nswitch_dev_full_report *fr;
	__u8 i;
	__u8 report_rel = 0;
	sax_calibration_data *sax = &ndev->calibration.sax;
	__s16 m[3];

	switch (ndev->state.input_report) {
	case REPLY:
	case SIMPLE:
	case NFC_UPDATE:
		hid_info(ndev->hdev, "I don't want this...");
		return;
	default:
		break;
	}

	i = 0;
	fr = &ndev->state.full;
	switch (ndev->info.type) {
	case LEFT_JOYCON:
		input_report_key(siminput, BTN_LEFT, fr->buttons.right);
		input_report_key(siminput, BTN_MIDDLE, fr->buttons.up);
		input_report_key(siminput, BTN_RIGHT, fr->buttons.down);
		report_rel = ndev->state.full.buttons.zl;
		break;
	case RIGHT_JOYCON:
		input_report_key(siminput, BTN_LEFT, fr->buttons.a);
		input_report_key(siminput, BTN_MIDDLE, fr->buttons.y);
		input_report_key(siminput, BTN_RIGHT, fr->buttons.b);
		report_rel = ndev->state.full.buttons.zr;
		break;
	default:
		return;
	}

	if (report_rel) {
		i = 0;
		while (i < 3) {
			m[i] = fr->ax6[0][1][i] - sax->gyroscope_origin[i];
			//m[i] = m[i] * ndev->calibration.gyro_coeff[i];
			++i;
		}
		input_report_rel(siminput, REL_X, m[2] / 20);
		input_report_rel(siminput, REL_Y, -(m[1] / 20));
	}
	input_sync(siminput);
}

/* Event Handler */
static void prepare_simple_joypad(nswitch_dev *ndev) {
	calibration_data *cd;
	unsigned int i;

	cd = &ndev->calibration;
	ndev->siminput = input_allocate_device();
	input_set_drvdata(ndev->siminput, ndev);
	ndev->siminput->dev.parent = &ndev->hdev->dev;
	ndev->siminput->id.bustype = ndev->hdev->bus;
	ndev->siminput->id.vendor = ndev->hdev->vendor;
	ndev->siminput->id.product = ndev->hdev->product;
	ndev->siminput->id.version = ndev->hdev->version;
	ndev->siminput->name = kasprintf(GFP_KERNEL, "%s Simple Emulated Joypad" , ndev->hdev->name);

	set_bit(EV_KEY, ndev->siminput->evbit);
	for (i = 0; i < ARRAY_SIZE(ns_simple_buttons); ++i)
		set_bit(ns_simple_buttons[i], ndev->siminput->keybit);

	set_bit(EV_ABS, ndev->siminput->evbit);

	set_bit(ABS_X, ndev->siminput->absbit);
	set_bit(ABS_Y, ndev->siminput->absbit);

	switch (ndev->info.type) {
	case LEFT_JOYCON:
		input_set_abs_params(ndev->siminput, ABS_X,
							 cd->left_stick.xcenter - cd->left_stick.xmin_offset,
							 cd->left_stick.xcenter + cd->left_stick.xmax_offset, 10, 0);
		input_set_abs_params(ndev->siminput, ABS_Y,
							 cd->left_stick.ycenter - cd->left_stick.ymin_offset,
							 cd->left_stick.ycenter + cd->left_stick.ymax_offset, 10, 0);
		break;
	case RIGHT_JOYCON:
		input_set_abs_params(ndev->siminput, ABS_X,
							 cd->right_stick.xcenter - cd->right_stick.xmin_offset,
							 cd->right_stick.xcenter + cd->right_stick.xmax_offset, 10, 0);
		input_set_abs_params(ndev->siminput, ABS_Y,
							 cd->right_stick.ycenter - cd->right_stick.ymin_offset,
							 cd->right_stick.ycenter + cd->right_stick.ymax_offset, 10, 0);
		break;
	default:
		hid_info(ndev->hdev, "Unknown joycon type at %p...", ndev);
		dump_mem(ndev->hdev, (void*)&ndev->info, sizeof(ndev->info));
		return;
	}
	ndev->handler = report_simple_keys;
	hid_info(ndev->hdev, "Handler set to report keys...");
	input_register_device(ndev->siminput);
	/* HAI CHIGAIMASU */
	ns_exchange(ndev, &(output_command) {
			BASIC, 0, 0, {}, SET_INPUT_REPORT_MODE, {
				.mode = STANDARD
			}
	});
}

/* Event handler */
static void prepare_simple_mouse(nswitch_dev *ndev) {
	ndev->siminput = input_allocate_device();
	input_set_drvdata(ndev->siminput, ndev);
	ndev->siminput->dev.parent = &ndev->hdev->dev;
	ndev->siminput->id.bustype = ndev->hdev->bus;
	ndev->siminput->id.vendor = ndev->hdev->vendor;
	ndev->siminput->id.product = ndev->hdev->product;
	ndev->siminput->id.version = ndev->hdev->version;
	ndev->siminput->name = kasprintf(GFP_KERNEL, "%s Simple Emulated Mouse" , ndev->hdev->name);

	set_bit(INPUT_PROP_POINTER, ndev->siminput->propbit);

	set_bit(EV_KEY, ndev->siminput->evbit);
	set_bit(EV_REL, ndev->siminput->evbit);

	set_bit(BTN_LEFT, ndev->siminput->keybit);
	set_bit(BTN_MIDDLE, ndev->siminput->keybit);
	set_bit(BTN_RIGHT, ndev->siminput->keybit);
	set_bit(REL_X, ndev->siminput->relbit);
	set_bit(REL_Y, ndev->siminput->relbit);

	ndev->handler = report_simple_mouse;
	hid_info(ndev->hdev, "Handler set to report movements...");
	input_register_device(ndev->siminput);
	/* HAI CHIGAIMASU */
	ns_exchange(ndev, &(output_command) {
		BASIC, 0, 0, {}, SET_IMU, {
			.imu_state = 1
		}
	});
	/* HAI CHIGAIMASU */
	ns_exchange(ndev, &(output_command) {
		BASIC, 0, 0, {}, SET_INPUT_REPORT_MODE, {
			.mode = STANDARD
		}
	});
}

/* Event Handler */
static void validate_dual(nswitch_dev *ndev) {
	if (ndev->info.type == LEFT_JOYCON)
		return;
	if (ndev->state.simple.down) {
		prepare_dual_joypad(ndev->right);
	} else if (ndev->state.simple.left) {
		ndev->right->handler = simplejc_prepare;
		ndev->handler = simplejc_prepare;
		ndev->right->right = 0;
		ndev->right = 0;
	}
}

/* Event Handler */
static void report_dual_keys(nswitch_dev *ndev) {
	struct input_dev *siminput = ndev->siminput;
	nswitch_dev_full_report *fr;
	stick_state *lss, *rss;
	__u8 i;
	const short *ev = ns_buttons;
	
	union {
		__u32 rbuttons;
		standard_button_state sbs;
	} buttons;

	switch (ndev->state.input_report) {
	case REPLY:
	case SIMPLE:
	case NFC_UPDATE:
		hid_info(ndev->hdev, "I don't want this...");
		return;
	default:
		break;
	}

	lss = &ndev->state.full.left_stick;
	rss = &ndev->right->state.full.right_stick;
	fr = &ndev->state.full;
	buttons.rbuttons = 0;
	buttons.sbs = fr->buttons;

	i = 0;
	while (i < 24) {
		input_report_key(siminput, *ev++, (buttons.rbuttons >> i) & 1);
		++i;
	}
	input_report_abs(siminput, ABS_X, lss->x);
	input_report_abs(siminput, ABS_Y, lss->y);
	input_report_abs(siminput, ABS_RX, rss->x);
	input_report_abs(siminput, ABS_RY, rss->y);
	input_sync(siminput);
}

/* Event Handler */
static void prepare_dual_joypad(nswitch_dev *ndev) {
	calibration_data *lcd, *rcd;
	nswitch_dev *rdev = ndev->right;
	unsigned int i;

	lcd = &ndev->calibration;
	rcd = &rdev->calibration;
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

	rdev->handler = ndev->handler = report_dual_keys;

	hid_info(ndev->hdev, "Handler set to report keys...");
	input_register_device(ndev->siminput);
	/* HAI CHIGAIMASU */
	ns_exchange(ndev, &(output_command) {
			BASIC, 0, 0, {}, SET_INPUT_REPORT_MODE, {
				.mode = STANDARD
	}});
	/* HAI CHIGAIMASU */
	ns_exchange(rdev, &(output_command) {
			BASIC, 0, 0, {}, SET_INPUT_REPORT_MODE, {
				.mode = STANDARD
	}});
}

/* Event Handler */
static void validate_simple(nswitch_dev *ndev) {
	if (ndev->state.simple.right) {
		hid_info(ndev->hdev, "RIGHT pressed...preparing joypad...");
		
		/* HAI CHIGAIMASU */
		handshake_rumble(ndev);
		/* HAI CHIGAIMASU */
		prepare_simple_joypad(ndev);
	} else if (ndev->state.simple.up) {
		hid_info(ndev->hdev, "UP pressed...preparing mouse...");
		/* HAI CHIGAIMASU */
		handshake_rumble(ndev);
		/* TODO: Implement mouse:
		   Use rel coord or abs coords?
		   compute x/y rel speed from Gyro speed
		 */
		prepare_simple_mouse(ndev);
	} else if (ndev->state.simple.down) {
		hid_info(ndev->hdev, "DOWN pressed... canceling association...");
		ndev->handler = simplejc_prepare;
	}
}

/* Event Handler */
void simplejc_prepare(nswitch_dev *ndev) {
	nswitch_list *nl;
	struct list_head *t;
	__u8 found = 0;

	if (ndev->state.simple.sl &&
		ndev->state.simple.sr) {
		hid_info(ndev->hdev, "SR+SL, validating simple mode...");
		ndev->handler = validate_simple;
		/* HAI CHIGAIMASU */
		handshake_rumble(ndev);
	} else if (ndev->info.type == LEFT_JOYCON &&
			   (ndev->state.simple.lr || ndev->state.simple.z)) {
		hid_info(ndev->hdev, "L or Z pressed on left joycon, searching for a right joycon...");
		nl = 0;
		t = &rjoycons;
		/**/
		list_for_each_entry(nl, t, list) {
			if (nl->ndev->handler != simplejc_prepare) {
				continue;
			}
			if (nl->ndev->state.simple.lr || nl->ndev->state.simple.z) {
				found = 1;
				break;
			}
		}
		if (found) {
			hid_info(ndev->hdev, "L and R, validating dual mode...");
			/* TODO: Lock here */
			ndev->right = nl->ndev;
			ndev->right->right = ndev;
			ndev->handler = validate_dual;
			nl->ndev->handler = validate_dual;
			/* SOSHITE CHIGAIMAAAAAAAAAASU */
			handshake_rumble(ndev);
		}
	}
}
