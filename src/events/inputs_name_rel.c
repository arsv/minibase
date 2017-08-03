#include <bits/input/rel.h>
#include <bits/input.h>
#include <util.h>

#include "inputs.h"

static const char* names[] = {
	[REL_X] = "X",
	[REL_Y] = "Y",
	[REL_Z] = "Z",
	[REL_RX] = "RX",
	[REL_RY] = "RY",
	[REL_RZ] = "RZ",
	[REL_HWHEEL] = "HWHEEL",
	[REL_DIAL] = "DIAL",
	[REL_WHEEL] = "WHEEL",
	[REL_MISC] = "MISC",
};

const struct ev ev_rel = {
	.type = EV_REL,
	.size = 2,
	.tag = "REL",
	.names = names,
	.count = ARRAY_SIZE(names)
};
