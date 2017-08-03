#include <bits/input/led.h>
#include <bits/input.h>
#include <util.h>

#include "inputs.h"

static const char* names[] = {
	[LED_NUML] = "NumLock",
	[LED_CAPSL] = "CapsLock",
	[LED_SCROLLL] = "ScrollLock",
	[LED_COMPOSE] = "Compose",
	[LED_KANA] = "kana",
	[LED_SLEEP] = "sleep",
	[LED_SUSPEND] = "suspend",
	[LED_MUTE] = "mute",
	[LED_MISC] = "misc",
	[LED_MAIL] = "mail",
	[LED_CHARGING] = "charging"
};

const struct ev ev_led = {
	.type = EV_LED,
	.size = 2,
	.tag = "LED",
	.names = names,
	.count = ARRAY_SIZE(names)
};
