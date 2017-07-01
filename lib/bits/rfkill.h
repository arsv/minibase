#include <bits/ints.h>

#define RFKILL_TYPE_WLAN       1
#define RFKILL_TYPE_BLUETOOTH  2
#define RFKILL_TYPE_UWB        3
#define RFKILL_TYPE_WIMAX      4
#define RFKILL_TYPE_WWAN       5
#define RFKILL_TYPE_GPS        6
#define RFKILL_TYPE_FM         7
#define RFKILL_TYPE_NFC        8

#define RFKILL_OP_ADD          0
#define RFKILL_OP_DEL          1
#define RFKILL_OP_CHANGE       2
#define RFKILL_OP_CHANGE_ALL   3

struct rfkill_event {
	int32_t idx;
	uint8_t type;
	uint8_t op;
	uint8_t soft;
	uint8_t hard;
} __attribute__((packed));

