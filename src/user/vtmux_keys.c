#include <bits/input.h>
#include <bits/major.h>

#include <sys/open.h>
#include <sys/read.h>
#include <sys/close.h>
#include <sys/fstat.h>
#include <sys/ioctl.h>
#include <sys/_exit.h>
#include <sys/getdents.h>

#include <string.h>
#include <format.h>
#include <fail.h>

#include "vtmux.h"

/* Keyboard event handling.
   The user presses Ctrl-Alt-Fn, vtmux switches to tty(n). */

/* Assumption: all keyboards generate somewhat PC-compatible
   keycodes. This is not as far from reality as one might expect
   even for hw scancodes, but evdev actually implements in-kernel
   translation table so what we get here are keycodes, *not* raw
   hw scancodes. It's probably up to udev to set up the tables. */

#define KEY_ESC   1
#define KEY_LCTL 29
#define KEY_LALT 56
#define KEY_F1   59
#define KEY_F10  68

/* The kernel only report press/release events, so modifier state
   must be tracked here. Ctrl-Alt-Fn switching logic is hardcoded,
   for now at least. */

#define MOD_LCTL (1<<0)
#define MOD_LALT (1<<1)

void keyrelease(struct kbd* kb, int code)
{
	switch(code) {
		case KEY_LALT: kb->mod &= ~MOD_LALT; break;
		case KEY_LCTL: kb->mod &= ~MOD_LCTL; break;
	}
}

void keypress(struct kbd* kb, int code)
{
	switch(code) {
		case KEY_LALT: kb->mod |= MOD_LALT; return;
		case KEY_LCTL: kb->mod |= MOD_LCTL; return;
	}

	int alt = kb->mod & MOD_LALT;
	int ctl = kb->mod & MOD_LCTL;

	if(!(ctl && alt)) return;

	switch(code) {
		case KEY_ESC:
			switchto(consoles[0].tty);
			break;
		case KEY_F1 ... KEY_F10:
			switchto(code - KEY_F1 + 1);
			break;
	}
}

void handlekbd(struct kbd* kb, int fd)
{
	char buf[256];
	char* ptr;
	int rd;

	while((rd = sysread(fd, buf, sizeof(buf))) > 0)
		for(ptr = buf; ptr < buf + rd; ptr += sizeof(struct event))
		{
			struct event* ev = (struct event*) ptr;

			if(ev->type != EV_KEY)
				continue;
			if(ev->value == 1)
				keypress(kb, ev->code);
			else if(ev->value == 0)
				keyrelease(kb, ev->code);
			/* value 2 is autorepeat, ignore */
		}
}

/* From all /dev/input/event* devices the directory-scanning code
   tries to open, we should only pick the keyboards that can generate
   KEY_ESC, KEY_F1 and so on. Some devices there are not keyboards
   at all, and some are but only generate one or two codes
   (like power button, lid sensor and such).
   
   Also while we're at that, ask the input driver to only send
   the keycodes we're interested in. This should prevent excessive
   wakeups during regular typing.

   The only documentation available for most of this stuff is
   in the kernel sources apparently. Refer to
   linux/include/uapi/input.h and linux/drivers/input/evdev.c. */

static int hascode(uint8_t* bits, int len, int code)
{
	if(code / 8 >= len)
		return 0;
	return bits[code/8] & (1 << (code % 8));
}

static void setcode(uint8_t* bits, int len, int code)
{
	if(code / 8 >= len)
		return;
	bits[code/8] |= (1 << (code % 8));
}

static int check_event_bits(int fd)
{
	uint8_t bits[32];
	int bitsize = sizeof(bits);

	memset(bits, 0, bitsize);

	if(sysioctl(fd, EVIOCGBIT(EV_KEY, bitsize), bits) < 0)
		return 0;

	int alt = hascode(bits, bitsize, KEY_LALT);
	int ctl = hascode(bits, bitsize, KEY_LCTL);
	int esc = hascode(bits, bitsize, KEY_ESC);
	int f1 = hascode(bits, bitsize, KEY_F1);

	return (ctl && alt && f1 && esc);
}

static void set_event_mask(int fd)
{
	uint8_t bits[32];
	int bitsize = sizeof(bits);

	struct input_mask mask = {
		.type = EV_KEY,
		.size = sizeof(bits),
		.ptr = (long)bits
	};

	memset(bits, 0, bitsize);

	setcode(bits, bitsize, KEY_LCTL);
	setcode(bits, bitsize, KEY_LALT);
	setcode(bits, bitsize, KEY_ESC);

	int i;
	for(i = 0; i < 10; i++)
		setcode(bits, bitsize, KEY_F1 + i);

	sysioctl(fd, EVIOCSMASK, &mask);

	memset(bits, 0, bitsize);
	mask.type = EV_MSC;

	sysioctl(fd, EVIOCSMASK, &mask);

	/* EV_SYN cannot be masked */
}

/* Directory-scanning code opens /dev/input/eventN and calls
   this to determine what to do with the fd: */

int prep_event_dev(int fd)
{
	if(!check_event_bits(fd))
		return 0; /* close device */

	set_event_mask(fd);

	return 1; /* add device to poll list */
}
