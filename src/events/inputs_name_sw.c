#include <bits/input/sws.h>
#include <bits/input.h>
#include <util.h>

#include "inputs.h"

static const char* names[] = {
	[SW_LID] = "LID",
	[SW_TABLET_MODE] = "TABLET",
	[SW_HEADPHONE_INSERT] = "HEADPHONE",
	[SW_RFKILL_ALL] = "RFKILL",
	[SW_MICROPHONE_INSERT] = "MICROPHONE",
	[SW_DOCK] = "DOCK",
	[SW_LINEOUT_INSERT] = "LINEOUT",
	[SW_JACK_PHYSICAL_INSERT] = "JACK",
	[SW_VIDEOOUT_INSERT] = "VIDEOOUT",
	[SW_CAMERA_LENS_COVER] = "CLENS_COVER",
	[SW_KEYPAD_SLIDE] = "KEYPAD_SLIDE",
	[SW_FRONT_PROXIMITY] = "PROXIMITY",
	[SW_ROTATE_LOCK] = "ROTATE_LOCK",
	[SW_LINEIN_INSERT] = "LINEIN",
	[SW_MUTE_DEVICE] = "MUTE",
	[SW_PEN_INSERTED] = "PEN"
};

const struct ev ev_sw = {
	.type = EV_SW,
	.size = 2,
	.tag = "SW",
	.names = names,
	.count = ARRAY_SIZE(names)
};
