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

/* Keyboard-driven switching: the user presses Ctrl-Alt-Fn, vtmux
   switches to tty(n). */

/* Assumption: all keyboards generate somewhat PC-compatible
   scancodes. This is not as far from reality as one might expect.
   In-kernel VT switching code must be doing something similar
   at some point too. */

#define KEY_LCTL 29
#define KEY_LALT 56
#define KEY_F1  59
#define KEY_F10 68

#define MOD_LCTL (1<<0)
#define MOD_LALT (1<<1)

/* The kernel only report press/release events, so modifier state
   must be tracked here. Ctrl-Alt-Fn switching logic is hardcoded,
   for now at least. */

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
		case KEY_F1 ... KEY_F10:
			switchto(code - KEY_F1 + 1);
			break;
	}
}

void handlekbd(int ki, int fd)
{
	struct kbd* kb = &keyboards[ki];

	char buf[256];
	char* ptr;
	int rd;

	while((rd = sysread(fd, buf, sizeof(buf))) > 0)
		for(ptr = buf; ptr < buf + rd; ptr += sizeof(struct event))
		{
			struct event* ev = (struct event*) ptr;

			if(ev->value == 1)
				keypress(kb, ev->code);
			else if(ev->value == 0)
				keyrelease(kb, ev->code);
			/* value 2 is autorepeat, ignore */
		}
}

/* Keyboard setup: go through /dev/input/event* nodes, and use
   those that may generate the key events vtmux needs. There are
   lots of useless nodes in /dev/input typically, so no point in
   keeping them all open, we only need the main keyboard(s) with
   at least Ctrl, Alt and F1 keys.

   This should probably not be done like this, it's really udev's
   job to classify devices, but we can't rely on udev actually
   being configured to do that yet.

   Finally, Linux allows masking input events, so we request the
   input drivers to only send the keycodes we're interested in.
   This should prevent excessive wakeups during regular typing.

   The only documentation available for most of this stuff is in
   the kernel sources apparently. Refer to linux/include/uapi/input.h
   and linux/drivers/input/evdev.c. */

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

	memset(bits, 0, sizeof(bits));

	if(sysioctl(fd, EVIOCGBIT(EV_KEY, 32), (long)bits) < 0)
		return 0;

	int blen = sizeof(bits);
	int alt = hascode(bits, blen, KEY_LALT);
	int ctl = hascode(bits, blen, KEY_LCTL);
	int f1 = hascode(bits, blen, KEY_F1);

	return (ctl && alt && f1);
}

static void set_event_mask(int fd)
{
	uint8_t bits[32];
	struct input_mask mask = {
		.type = EV_KEY,
		.size = sizeof(bits),
		.ptr = (long)bits
	};

	memset(bits, 0, sizeof(bits));

	int blen = sizeof(bits);
	setcode(bits, blen, KEY_LCTL);
	setcode(bits, blen, KEY_LALT);
	setcode(bits, blen, KEY_F1);

	sysioctl(fd, EVIOCSMASK, (long)&mask);

	memset(bits, 0, sizeof(bits));
	mask.type = EV_MSC;

	sysioctl(fd, EVIOCSMASK, (long)&mask);
}

static int check_event_dev(int fd)
{
	struct stat st;

	if(sysfstat(fd, &st) < 0)
		return 0;
	if(major(st.st_rdev) != INPUT_MAJOR)
		return 0;
	if(!check_event_bits(fd))
		return 0;

	return 1;
}

static void add_keyboard(int fd, struct kbd* kb)
{
	set_event_mask(fd);

	kb->fd = fd;
	kb->dev = 0;
	kb->mod = 0;
}

static void check_dir_ent(char* dir, char* name)
{
	if(nkeyboards >= KEYBOARDS)
		return;

	int dirlen = strlen(dir);
	int namelen = strlen(name);
	char path[dirlen + namelen + 2];

	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);

	int fd = sysopen(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

	if(fd < 0)
		return;

	if(check_event_dev(fd))
		add_keyboard(fd, &keyboards[nkeyboards++]);
	else
		sysclose(fd);
}

static int dotddot(char* p)
{
	if(!p[0])
		return 1;
	if(p[0] == '.' && !p[1])
		return 1;
	if(p[1] == '.' && !p[2])
		return 1;
	return 0;
}

void setup_keyboards(void)
{
	char debuf[1024];
	char* dir = "/dev/input";

	long fd = sysopen(dir, O_RDONLY | O_DIRECTORY);

	if(fd < 0)
		fail("cannot open", dir, fd);

	long rd;

	while((rd = sysgetdents64(fd, debuf, sizeof(debuf))) > 0) {
		char* ptr = debuf;
		char* end = debuf + rd;
		while(ptr < end) {
			struct dirent64* de = (struct dirent64*) ptr;
			ptr += de->d_reclen;

			if(dotddot(de->d_name))
				continue;
			if(!de->d_reclen)
				break;
			if(de->d_type != DT_UNKNOWN && de->d_type != DT_CHR)
				continue;

			check_dir_ent(dir, de->d_name);
		}
	}

	sysclose(fd);
}
