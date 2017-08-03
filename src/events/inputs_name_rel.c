#include <bits/input/rel.h>
#include <bits/input.h>
#include <util.h>

#include "inputs.h"

static const char* names[] = {
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

const struct ev ev_rel = {
	.type = EV_REL,
	.size = 2,
	.tag = "REL",
	.names = names,
	.count = ARRAY_SIZE(names)
};
