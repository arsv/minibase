#include <bits/input/led.h>
#include <bits/input.h>
#include <util.h>

#include "inputs.h"

static const char* names[] = {
	[LED_NUML] = "NUML",
	[LED_CAPSL] = "CAPSL",
	[LED_SCROLLL] = "SCROLLL",
	[LED_COMPOSE] = "COMPOSE",
	[LED_KANA] = "KANA",
	[LED_SLEEP] = "SLEEP",
	[LED_SUSPEND] = "SUSPEND",
	[LED_MUTE] = "MUTE",
	[LED_MISC] = "MISC",
	[LED_MAIL] = "MAIL",
	[LED_CHARGING] = "CHARGING",
	[LED_MAX] = "MAX"
};

const struct ev ev_led = {
	.type = EV_LED,
	.size = 2,
	.tag = "LED",
	.names = names,
	.count = ARRAY_SIZE(names)
};
