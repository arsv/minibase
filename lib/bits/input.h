#include <bits/time.h>
#include <bits/ioctl/input.h>

#define EV_SYN   0x00
#define EV_KEY   0x01
#define EV_REL   0x02
#define EV_ABS   0x03
#define EV_MSC   0x04
#define EV_SW    0x05
#define EV_LED   0x11
#define EV_SND   0x12
#define EV_REP   0x14
#define EV_FF    0x15
#define EV_PWR   0x16

struct event {
	struct timeval tv;
	unsigned short type;
	unsigned short code;
	unsigned int value;
};
