#include <bits/input/abs.h>
#include <bits/input.h>
#include <util.h>

#include "inputs.h"

static const char* names[] = {
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

const struct ev ev_abs = {
	.type = EV_ABS,
	.size = 8,
	.tag = "ABS",
	.names = names,
	.count = ARRAY_SIZE(names)
};
