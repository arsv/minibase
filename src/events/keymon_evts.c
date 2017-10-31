#include <bits/major.h>
#include <bits/input.h>
#include <bits/input/key.h>

#include <sys/file.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/dents.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "keymon.h"

/* The kernel only report press/release events, so modifier state
   must be tracked here. Only few known modifier keys are tracked
   to simplify the code. Tracking arbitrary keys would require lots
   of code, and won't be used most of the time. */

static int modifiers;

static void spawn(struct action* ka)
{
	int pid, ret, status;
	char* cmd = ka->cmd;
	char* arg = *(ka->arg) ? ka->arg : NULL;

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return;
	} else if(pid == 0) {
		char* argv[] = { cmd, arg, NULL };
		ret = execvpe(*argv, argv, environ);
		fail("exec", *argv, ret);
	}

	ret = sys_waitpid(pid, &status, 0);

	if(ret > 0)
		return;
	if(ret < 0)
		sys_kill(pid, SIGTERM);
}

static void set_global_mods(void)
{
	struct device* kb;
	int mods = 0;

	for(kb = devices; kb < devices + ndevices; kb++) {
		if(kb->mods & KEYM_LCTRL)
			mods |= MODE_CTRL;
		if(kb->mods & KEYM_LALT)
			mods |= MODE_ALT;
	}

	modifiers = mods;
}

static void start_hold(struct action* ka, struct device* kb)
{
	if(ka->mode & MODE_LONG)
		ka->time = LONGTIME;
	else
		ka->time = HOLDTIME;

	ka->minor = kb->minor;
}

static void reset_hold(struct action* ka)
{
	ka->time = 0;
	ka->minor = 0;
}

void hold_done(struct action* ka)
{
	ka->time = 0;
	ka->minor = 0;

	spawn(ka);
}

static void key_release(struct device* kb, int code)
{
	struct action* ka;
	int mods = kb->mods;

	for(ka = actions; ka < actions + nactions; ka++) {
		if(!ka->time)
			continue;
		else if(ka->code != code)
			continue;
		else if(ka->minor != kb->minor)
			continue;
		else reset_hold(ka);
	}

	switch(code) {
		case KEY_LEFTALT:  mods &= ~KEYM_LALT;  break;
		case KEY_LEFTCTRL: mods &= ~KEYM_LCTRL; break;
		case KEY_RIGHTALT: mods &= ~KEYM_RALT;  break;
		case KEY_RIGHTCTRL:mods &= ~KEYM_RCTRL; break;
		default: return;
	}

	kb->mods = mods;

	set_global_mods();
}

static void key_press(struct device* kb, int code)
{
	int mask = MODE_CTRL | MODE_ALT;
	struct action* ka;

	for(ka = actions; ka < actions + nactions; ka++) {
		if(ka->code != code)
			continue;
		if((ka->mode & mask) != (modifiers & mask))
			continue;
		if(ka->mode & MODE_HOLD)
			start_hold(ka, kb);
		else
			spawn(ka);
	}

	int mods = kb->mods;

	switch(code) {
		case KEY_LEFTALT:  mods |= KEYM_LALT;  break;
		case KEY_LEFTCTRL: mods |= KEYM_LCTRL; break;
		case KEY_RIGHTALT: mods |= KEYM_RALT;  break;
		case KEY_RIGHTCTRL:mods |= KEYM_RCTRL; break;
		default: return;
	}

	kb->mods = mods;

	set_global_mods();
}

static void handle_event(struct device* kb, struct event* ev)
{
	int value = ev->value;
	int type = ev->type;
	int code = ev->code;

	if(type == EV_KEY) {
		if(value == 1)
			key_press(kb, code);
		else if(!value)
			key_release(kb, code);
	} else if(type == EV_SW) {
		if(value == 1)
			key_press(kb, code | CODE_SWITCH);
		else if(!value)
			key_release(kb, code | CODE_SWITCH);
	}
}

void handle_input(struct device* kb, int fd)
{
	char buf[256];
	char* ptr;
	int size = sizeof(struct event);
	int rd;

	while((rd = sys_read(fd, buf, sizeof(buf))) > 0)
		for(ptr = buf; ptr < buf + rd; ptr += size)
			handle_event(kb, (struct event*) ptr);
}
