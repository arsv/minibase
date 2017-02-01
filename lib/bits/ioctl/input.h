#include <bits/ioctl.h>

#define EVIOCGRAB      _IOW('E', 0x90, int)
#define EVIOCREVOKE    _IOW('E', 0x91, int)

#define EVIOCGBIT(ev,len) _IOC(_IOC_R, 'E', 0x20 + (ev), len)

struct input_mask {
	uint32_t type;
	uint32_t size;
	uint64_t ptr;
};

#define EVIOCSMASK _IOW('E', 0x93, struct input_mask)	/* Set event-masks */
