#include <bits/major.h>
#include <bits/input.h>
#include <bits/input/key.h>

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/dents.h>

#include <string.h>
#include <format.h>
#include <exit.h>
#include <fail.h>

#include "keymon.h"

/* From all /dev/input/event* devices the directory-scanning code
   tries to open, we should only pick the keyboards that can generate
   the configured event. Some devices there are not keyboards at all,
   and some are but only generate one or two codes.

   What's more important, we ask the input driver to only send
   the keycodes we are interested in. This should prevent excessive
   wakeups during regular typing.

   The only documentation available for most of this stuff is in
   the kernel sources apparently. Refer to
   linux/include/uapi/input.h and linux/drivers/input/evdev.c. */

static int hascode(char* bits, int len, int code)
{
	if(code <= 0)
		return 0;
	if(code / 8 >= len)
		return 0;

	return bits[code/8] & (1 << (code % 8));
}

static void setcode(char* bits, int len, int code)
{
	if(code <= 0)
		return;
	if(code / 8 >= len)
		return;

	bits[code/8] |= (1 << (code % 8));
}

static void check_keyact(struct action* ka, char* bits, char* need, int size)
{
	if(!hascode(bits, size, ka->code))
		return;
	if(ka->mode & MODE_CTRL && !hascode(bits, size, KEY_LEFTCTRL))
		return;
	if(ka->mode & MODE_ALT  && !hascode(bits, size, KEY_LEFTALT))
		return;

	setcode(need, size, ka->code);

	if(ka->mode & MODE_CTRL)
		setcode(need, size, KEY_LEFTCTRL);
	if(ka->mode & MODE_ALT)
		setcode(need, size, KEY_LEFTALT);
}

static void check_swact(struct action* sa, char* bits, char* need, int size)
{
	if(!hascode(bits, size, sa->code))
		return;

	setcode(need, size, sa->code);
}

static int get_evt_mask(int fd, int type, char* bits, int size)
{
	return sys_ioctl(fd, EVIOCGBIT(type, size), bits);
}

static int set_evt_mask(int fd, int type, char* bits, int size)
{
	int ret;

	struct input_mask mask = {
		.type = type,
		.size = size,
		.ptr = (long)bits
	};

	if((ret = sys_ioctl(fd, EVIOCSMASK, &mask)) < 0)
		warn("ioctl", "EVIOCSMASK", ret);

	return ret;
}

static void mask_msc_events(int fd)
{
	char bits[32];
	int ret;

	memzero(bits, sizeof(bits));

	struct input_mask mask = {
		.type = EV_MSC,
		.size = sizeof(bits),
		.ptr = (long)bits
	};

	if((ret = sys_ioctl(fd, EVIOCSMASK, &mask)) < 0)
		warn("ioctl", "EVIOCGBIT EV_MSC", ret);
}

static void check_all_keyacts(char* bits, char* need, int size)
{
	struct action* ka;

	for(ka = actions; ka < actions + nactions; ka++)
		if(ka->code > 0)
			check_keyact(ka, bits, need, size);
}

static void check_all_swacts(char* bits, char* need, int size)
{
	struct action* sa;

	for(sa = actions; sa < actions + nactions; sa++)
		if(sa->code < 0)
			check_swact(sa, bits, need, size);
}

static int check_evt_bits(int fd, int type, int size,
                          void (*check)(char* bits, char* need, int size))
{
	char bits[size];
	char need[size];
	int ret;

	memzero(bits, size);
	memzero(need, size);

	if((ret = get_evt_mask(fd, type, bits, size)) < 0)
		return ret;

	check(bits, need, size);

	if(!nonzero(need, size))
		return 0;

	if((ret = set_evt_mask(fd, type, need, size)) < 0)
		return ret;

	return 1;
}

/* Directory-scanning code opens /dev/input/eventN and calls
   this to determine what to do with the fd. Zero means drop,
   positive means poll this device. */ 

int try_event_dev(int fd)
{
	int ret, got = 0;

	if((ret = check_evt_bits(fd, EV_KEY, 32, check_all_keyacts)) < 0)
		return 0;
	else
		got += ret;

	if((ret = check_evt_bits(fd, EV_SW,   2, check_all_swacts)) < 0)
		return 0;
	else
		got += ret;

	if(got)
		mask_msc_events(fd);

	return got;
}
