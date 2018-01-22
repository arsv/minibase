#include <bits/types.h>
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

#define EVIOCGNAME(len)		_IOC(_IOC_R, 'E', 0x06, len)		/* get device name */
#define EVIOCGPHYS(len)		_IOC(_IOC_R, 'E', 0x07, len)		/* get physical location */
#define EVIOCGUNIQ(len)		_IOC(_IOC_R, 'E', 0x08, len)		/* get unique identifier */
#define EVIOCGPROP(len)		_IOC(_IOC_R, 'E', 0x09, len)		/* get device properties */
