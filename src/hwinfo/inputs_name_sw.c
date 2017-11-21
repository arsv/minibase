#include <bits/input/sws.h>
#include <bits/input.h>
#include <util.h>

#include "inputs.h"

static const char* names[] = {
	[SW_LID] = "lid",
	[SW_TABLET_MODE] = "tablet",
	[SW_HEADPHONE_INSERT] = "headphone",
	[SW_RFKILL_ALL] = "rfkill",
	[SW_MICROPHONE_INSERT] = "microphone",
	[SW_DOCK] = "dock",
	[SW_LINEOUT_INSERT] = "lineout",
	[SW_JACK_PHYSICAL_INSERT] = "jack",
	[SW_VIDEOOUT_INSERT] = "videoout",
	[SW_CAMERA_LENS_COVER] = "lens-cover",
	[SW_KEYPAD_SLIDE] = "keypad-slide",
	[SW_FRONT_PROXIMITY] = "proximity",
	[SW_ROTATE_LOCK] = "rotate-lock",
	[SW_LINEIN_INSERT] = "linein",
	[SW_MUTE_DEVICE] = "mute",
	[SW_PEN_INSERTED] = "pen"
};

const struct ev ev_sw = {
	.type = EV_SW,
	.size = 2,
	.tag = "SW",
	.names = names,
	.count = ARRAY_SIZE(names)
};
