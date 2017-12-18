#include <bits/input/abs.h>
#include <bits/input/led.h>
#include <bits/input/rel.h>
#include <bits/input/sws.h>
#include <bits/input.h>
#include <util.h>

#include "inputs.h"

static const char* abs[] = {
	[ABS_X] = "X",
	[ABS_Y] = "Y",
	[ABS_Z] = "Z",
	[ABS_RX] = "rX",
	[ABS_RY] = "rY",
	[ABS_RZ] = "rZ",
	[ABS_THROTTLE] = "throttle",
	[ABS_RUDDER] = "rudder",
	[ABS_WHEEL] = "wheel",
	[ABS_GAS] = "gas",
	[ABS_BRAKE] = "brake",
	[ABS_HAT0X] = "hat0-X",
	[ABS_HAT0Y] = "hat0-Y",
	[ABS_HAT1X] = "hat1-X",
	[ABS_HAT1Y] = "hat1-Y",
	[ABS_HAT2X] = "hat2-X",
	[ABS_HAT2Y] = "hat2-Y",
	[ABS_HAT3X] = "hat3-X",
	[ABS_HAT3Y] = "hat3-Y",
	[ABS_PRESSURE] = "pressure",
	[ABS_DISTANCE] = "distance",
	[ABS_TILT_X] = "tilt-X",
	[ABS_TILT_Y] = "tilt-Y",
	[ABS_TOOL_WIDTH] = "tool-width",
	[ABS_VOLUME] = "volume",
	[ABS_MISC] = "misc",
	[ABS_MT_SLOT] = "MT-slot",
	[ABS_MT_TOUCH_MAJOR] = "MT-touch-major",
	[ABS_MT_TOUCH_MINOR] = "MT-touch-minor",
	[ABS_MT_WIDTH_MAJOR] = "MT-width-major",
	[ABS_MT_WIDTH_MINOR] = "MT-width-minor",
	[ABS_MT_ORIENTATION] = "MT-orientation",
	[ABS_MT_POSITION_X] = "MT-position-X",
	[ABS_MT_POSITION_Y] = "MT-position-Y",
	[ABS_MT_TOOL_TYPE] = "MT-tool-type",
	[ABS_MT_BLOB_ID] = "MT-blob-ID",
	[ABS_MT_TRACKING_ID] = "MT-tracking-ID",
	[ABS_MT_PRESSURE] = "MT-pressure",
	[ABS_MT_DISTANCE] = "MT-distance",
	[ABS_MT_TOOL_X] = "MT-tool-X",
	[ABS_MT_TOOL_Y] = "MT-tool-Y"
};

static const char* rel[] = {
	[REL_X] = "X",
	[REL_Y] = "Y",
	[REL_Z] = "Z",
	[REL_RX] = "rX",
	[REL_RY] = "rY",
	[REL_RZ] = "rZ",
	[REL_HWHEEL] = "hwheel",
	[REL_DIAL] = "dial",
	[REL_WHEEL] = "wheel",
	[REL_MISC] = "misc",
};

static const char* led[] = {
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

static const char* sw[] = {
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

EVNAMES("ABS", abs);
EVNAMES("REL", rel);
EVNAMES("LED", led);
EVNAMES("SW ", sw);
