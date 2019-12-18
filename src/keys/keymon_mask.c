#include <bits/major.h>
#include <bits/input.h>
#include <bits/input/key.h>

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/dents.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "keymon.h"

/* From all /dev/input/event* devices the directory-scanning code
   tries to open, we should only pick the keyboards that can generate
   the configured events. Some devices there are not keyboards at all,
   some are but only generate one or two codes.

   What's more important, we ask the input driver to only send
   the keycodes we are interested in. This should prevent excessive
   wakeups during regular typing.

   The only documentation available for most of this stuff is
   in the kernel sources apparently. Refer to linux/include/uapi/input.h
   and linux/drivers/input/evdev.c. */

static int hascode(byte* bits, int len, int code)
{
	if(code <= 0)
		return 0;
	if(code / 8 >= len)
		return 0;

	return bits[code/8] & (1 << (code % 8));
}

static void setcode(byte* bits, int len, int code)
{
	if(code <= 0)
		return;
	if(code / 8 >= len)
		return;

	bits[code/8] |= (1 << (code % 8));
}

static void usecode(byte* bits, byte* need, int size, int code)
{
	if(!hascode(bits, size, code))
		return;

	setcode(need, size, code);
}

static void check_all_keyacts(CTX, byte* bits, byte* need, int size)
{
	struct act* ka;

	for(ka = first(ctx); ka; ka = next(ctx, ka)) {
		int code = ka->code;

		if(code & CODE_SWITCH) /* it's a switch action */
			continue;

		usecode(bits, need, size, code);
	}

	usecode(bits, need, size, KEY_LEFTCTRL);
	usecode(bits, need, size, KEY_LEFTALT);
}

static void check_all_swacts(CTX, byte* bits, byte* need, int size)
{
	struct act* ka;

	for(ka = first(ctx); ka; ka = next(ctx, ka)) {
		int raw = ka->code;
		int code = raw & ~CODE_SWITCH;

		if(raw == code) /* it's a key action */
			continue;

		setcode(need, size, code);
	}
}

static int get_evt_mask(int fd, int type, byte* bits, int size)
{
	return sys_ioctl(fd, EVIOCGBIT(type, size), bits);
}

static int set_evt_mask(int fd, int type, byte* bits, int size)
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

static int check_key_bits(CTX, int fd)
{
	int type = EV_KEY;
	byte bits[32];
	byte need[32];
	int size = sizeof(bits);
	int ret;

	memzero(bits, size);
	memzero(need, size);

	if((ret = get_evt_mask(fd, type, bits, size)) < 0)
		return ret;

	check_all_keyacts(ctx, bits, need, size);

	if(!nonzero(need, size))
		return 0;

	if((ret = set_evt_mask(fd, type, need, size)) < 0)
		return ret;

	return 1;
}

static int check_sw_bits(CTX, int fd)
{
	int type = EV_SW;
	byte bits[2];
	byte need[2];
	int size = sizeof(bits);
	int ret;

	memzero(bits, size);
	memzero(need, size);

	if((ret = get_evt_mask(fd, type, bits, size)) < 0)
		return ret;

	check_all_swacts(ctx, bits, need, size);

	if(!nonzero(need, size))
		return 0;

	if((ret = set_evt_mask(fd, type, need, size)) < 0)
		return ret;

	return 1;
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

/* Directory-scanning code opens /dev/input/eventN and calls
   this to determine what to do with the fd. Zero means drop,
   non-zero (positive) means poll this device. */

int try_event_dev(CTX, int fd)
{
	int ret, got = 0;

	if((ret = check_key_bits(ctx, fd)) < 0)
		return 0;
	else
		got += ret;

	if((ret = check_sw_bits(ctx, fd)) < 0)
		return 0;
	else
		got += ret;

	if(got) mask_msc_events(fd);

	return got;
}
