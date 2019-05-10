#include "hid-nswitch.h"

void set_leds(nswitch_dev *ndev, __u8 w) {
	ns_exchange(ndev, &(output_command) {
		BASIC, 0, 0, {}, SET_PLAYER_LIGHTS, {
			.player_lights = w
		}
	});
	ndev->ledcache = w;
}

static enum led_brightness nswitch_get_led(struct led_classdev *led_dev) {
	struct device *dev = led_dev->dev->parent;
	nswitch_dev *ndev = hid_get_drvdata(to_hid_device(dev));
	struct led_classdev *first = ndev->player_leds;
	int i = led_dev - first;
	int v = 0;
	nswitch_dev_input_report ret;
	subcmd_input *reply = &ret.full.reply;

	ret = ns_exchange(ndev, &(output_command) {
			BASIC, 0, 0, {}, GET_PLAYER_LIGHTS, {}
	});
	v = 1 << i;
	v &= reply->data[0] | (reply->data[0] >> 4);
	ndev->ledcache = reply->data[0] & 0x0F;
	return v ? LED_FULL : LED_OFF;
}

static void nswitch_set_led(struct led_classdev *led_dev,
							enum led_brightness v) {
	struct device *dev = led_dev->dev->parent;
	nswitch_dev *ndev = hid_get_drvdata(to_hid_device(dev));
	struct led_classdev *first = ndev->player_leds;
	int i = led_dev - first;
	int w;

	/* We can't send/receive commands while unplugging, so just
	   pretend this is fine.
	   Unplugging will turn off the leds anyway.
	*/
	if (led_dev->flags & LED_UNREGISTERING)
		return;

	w = ndev->ledcache;
	if (v)
		w |= 1 << i;
	else
		w &= ~(1 << i);
	hid_info(ndev->hdev, "Setting led #%d. Cache: %02x. New Mask: %02x\n",
			 i, ndev->ledcache, w);

	set_leds(ndev, w);
}

int nswitch_battery_get_property(struct power_supply *psy,
										enum power_supply_property psp,
										union power_supply_propval *val)
{
	nswitch_dev *ndev = power_supply_get_drvdata(psy);
	int ret = 0;
	nswitch_dev_input_report res;
	__u16 bat;

	if (psp == POWER_SUPPLY_PROP_SCOPE) {
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		return 0;
	} else if (psp != POWER_SUPPLY_PROP_CAPACITY) {
		return -EINVAL;
	}

	res = ns_exchange(ndev, &(output_command) {
		BASIC, 0, 0, {}, GET_VOLTAGE, {}
	});

	bat = *(__u16*)res.full.reply.data;
	val->intval = (bat - 1320) * 100 / 360;
	return ret;
}

static enum power_supply_property nswitch_battery_props[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SCOPE,
};

int init_battery(nswitch_dev *ndev) {
	struct power_supply_config psy_cfg = { .drv_data = ndev, };
	int ret;

	ndev->battery_desc.properties = nswitch_battery_props;
	ndev->battery_desc.num_properties = ARRAY_SIZE(nswitch_battery_props);
	ndev->battery_desc.get_property = nswitch_battery_get_property;
	ndev->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	ndev->battery_desc.use_for_apm = 0;
	ndev->battery_desc.name = kasprintf(GFP_KERNEL, "nswitch_battery_%s", ndev->hdev->uniq);
	if (!ndev->battery_desc.name)
		return -ENOMEM;

	ndev->battery = power_supply_register(&ndev->hdev->dev,
										  &ndev->battery_desc,
										  &psy_cfg);
	if (IS_ERR(ndev->battery)) {
		hid_err(ndev->hdev, "cannot register battery device\n");
		ret = PTR_ERR(ndev->battery);
		goto err_free;
	}

	power_supply_powers(ndev->battery, &ndev->hdev->dev);
	return 0;

err_free:
	kfree(ndev->battery_desc.name);
	ndev->battery_desc.name = NULL;
	return ret;
}

int init_player_leds(nswitch_dev *ndev) {
	struct device *dev = &ndev->hdev->dev;
	struct led_classdev *led;
	int ret;
	__u8 i;

	ret = 0;
	for (i = 0; i < 4; ++i) {
		led = &ndev->player_leds[i];
		if (!led)
			return -ENOMEM;

		led->name = kasprintf(GFP_KERNEL, "%s:green:p%u", dev_name(dev), i);
		led->brightness = 0;
		led->max_brightness = 1;
		led->brightness_get = nswitch_get_led;
		led->brightness_set = nswitch_set_led;
		ret = led_classdev_register(dev, led);
		if (ret)
			goto err_free;
		continue;
	err_free:
		kfree(led->name);
		return ret;
	}
	return ret;
}
