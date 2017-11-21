#include <bits/input/key.h>
#include <bits/input/sws.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "keymon.h"

/* There are much more keys than listed here, but handling those with
   keymon is not a good idea. So the list here is only for the keys
   that are expected to be handle by keymon on regular basis. Anything
   else can be configure with raw integer keycodes. */

static const struct key {
	char name[16];
	int code;
} keys[] = {
	{ "Esc",         KEY_ESC             },
	{ "Minus",       KEY_MINUS           },
	{ "Equal",       KEY_EQUAL           },
	{ "Backspace",   KEY_BACKSPACE       },
	{ "Backslash",   KEY_BACKSLASH       },
	{ "NumLock",     KEY_NUMLOCK         },
	{ "ScrollLock",  KEY_SCROLLLOCK      },
	{ "Space",       KEY_SPACE           },
	{ "Del",         KEY_DELETE          },
	{ "Delete",      KEY_DELETE          },

	{ "F1",          KEY_F1              },
	{ "F2",          KEY_F2              },
	{ "F3",          KEY_F3              },
	{ "F4",          KEY_F4              },
	{ "F5",          KEY_F5              },
	{ "F6",          KEY_F6              },
	{ "F7",          KEY_F7              },
	{ "F8",          KEY_F8              },
	{ "F9",          KEY_F9              },
	{ "F10",         KEY_F10             },
	{ "F11",         KEY_F11             },
	{ "F12",         KEY_F12             },

	{ "SysRq",       KEY_SYSRQ           },
	{ "Mute",        KEY_MUTE            },
	{ "Volume-",     KEY_VOLUMEDOWN      },
	{ "Volume+",     KEY_VOLUMEUP        },
	{ "Power",       KEY_POWER           },
	{ "Pause",       KEY_PAUSE           },
	{ "Sleep",       KEY_SLEEP           },
	{ "Stop",        KEY_STOP            },
	{ "Setup",       KEY_SETUP           },
	{ "Scale",       KEY_SCALE           },
	{ "WakeUp",      KEY_WAKEUP          },
	{ "Brightness-", KEY_BRIGHTNESSDOWN  },
	{ "Brightness+", KEY_BRIGHTNESSUP    },
	{ "WWAN",        KEY_WWAN            },
	{ "RFKill",      KEY_RFKILL          },
	{ "MicMute",     KEY_MICMUTE         },
	{ "Battery",     KEY_BATTERY         },
	{ "Bluetooth",   KEY_BLUETOOTH       },
	{ "WLAN",        KEY_WLAN            },
	{ "UWB",         KEY_UWB             },
	{ "",            0                   }
};

static const struct sw {
	char name[12];
	int code;
} sws[] = {
	{ "Lid",         SW_LID              },
	{ "Tablet",      SW_TABLET_MODE      },
	{ "Headphone",   SW_HEADPHONE_INSERT },
	{ "Dock",        SW_DOCK             }
};

int find_key(char* name)
{
	const struct key* ky;
	const struct sw* sw;
	int code;
	char* p;

	if((p = parseint(name, &code)) && !*p)
		return code;

	for(ky = keys; ky < ARRAY_END(keys); ky++)
		if(!strncmp(ky->name, name, sizeof(ky->name)))
			return ky->code;

	for(sw = sws; sw < ARRAY_END(sws); sw++)
		if(!strncmp(sw->name, name, sizeof(sw->name)))
			return sw->code | CODE_SWITCH;

	return 0;
}
