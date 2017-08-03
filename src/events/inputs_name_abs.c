#include <bits/input/abs.h>
#include <bits/input.h>
#include <util.h>

#include "inputs.h"

static const char* names[] = {
	[ABS_X] = "X",
	[ABS_Y] = "Y",
	[ABS_Z] = "Z",
	[ABS_RX] = "RX",
	[ABS_RY] = "RY",
	[ABS_RZ] = "RZ",
	[ABS_THROTTLE] = "THROTTLE",
	[ABS_RUDDER] = "RUDDER",
	[ABS_WHEEL] = "WHEEL",
	[ABS_GAS] = "GAS",
	[ABS_BRAKE] = "BRAKE",
	[ABS_HAT0X] = "HAT0X",
	[ABS_HAT0Y] = "HAT0Y",
	[ABS_HAT1X] = "HAT1X",
	[ABS_HAT1Y] = "HAT1Y",
	[ABS_HAT2X] = "HAT2X",
	[ABS_HAT2Y] = "HAT2Y",
	[ABS_HAT3X] = "HAT3X",
	[ABS_HAT3Y] = "HAT3Y",
	[ABS_PRESSURE] = "PRESSURE",
	[ABS_DISTANCE] = "DISTANCE",
	[ABS_TILT_X] = "TILT_X",
	[ABS_TILT_Y] = "TILT_Y",
	[ABS_TOOL_WIDTH] = "TOOL_WIDTH",
	[ABS_VOLUME] = "VOLUME",
	[ABS_MISC] = "MISC",
	[ABS_MT_SLOT] = "MT_SLOT",
	[ABS_MT_TOUCH_MAJOR] = "MT_TOUCH_MAJOR",
	[ABS_MT_TOUCH_MINOR] = "MT_TOUCH_MINOR",
	[ABS_MT_WIDTH_MAJOR] = "MT_WIDTH_MAJOR",
	[ABS_MT_WIDTH_MINOR] = "MT_WIDTH_MINOR",
	[ABS_MT_ORIENTATION] = "MT_ORIENTATION",
	[ABS_MT_POSITION_X] = "MT_POSITION_X",
	[ABS_MT_POSITION_Y] = "MT_POSITION_Y",
	[ABS_MT_TOOL_TYPE] = "MT_TOOL_TYPE",
	[ABS_MT_BLOB_ID] = "MT_BLOB_ID",
	[ABS_MT_TRACKING_ID] = "MT_TRACKING_ID",
	[ABS_MT_PRESSURE] = "MT_PRESSURE",
	[ABS_MT_DISTANCE] = "MT_DISTANCE",
	[ABS_MT_TOOL_X] = "MT_TOOL_X",
	[ABS_MT_TOOL_Y] = "MT_TOOL_Y"
};

const struct ev ev_abs = {
	.type = EV_ABS,
	.size = 8,
	.tag = "ABS",
	.names = names,
	.count = ARRAY_SIZE(names)
};
