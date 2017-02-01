#include <bits/time.h>
#include <bits/ioctl/input.h>

#define EV_KEY 0x01

struct event {
	struct timeval tv;
	unsigned short type;
	unsigned short code;
	unsigned int value;
};
