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
#include <exit.h>
#include <fail.h>

#include "config.h"
#include "keymon.h"

/* The kernel only report press/release events, so modifier state
   must be tracked here. Only few known modifier keys are tracked
   to simplify the code. Tracking arbitrary keys would require lots
   of code, and won't be used most of the time. */

static char cmdbuf[CMDLEN];

static void prep_action_path(char* action)
{
	char* p = cmdbuf;
	char* e = cmdbuf + sizeof(cmdbuf) - 1;

	p = fmtstr(p, e, CONFDIR);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, action);
	*p++ = '\0';
}

static void spawn(struct action* ka)
{
	int pid, ret, status;
	char* arg = *(ka->arg) ? ka->arg : NULL;

	prep_action_path(ka->cmd);

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return;
	} else if(pid == 0) {
		char* argv[] = { cmdbuf, arg, NULL };
		ret = sys_execve(*argv, argv, environ);
		fail("exec", *argv, ret);
	}

	ret = sys_waitpid(pid, &status, 0);

	if(ret > 0)
		return;
	if(ret < 0)
		sys_kill(pid, SIGTERM);
}

static void set_mods(struct device* kb, int mods)
{
	mods &= ~(MODE_ALT | MODE_CTRL);

	if(mods & (MOD_LCTRL | MOD_RCTRL))
		mods |= MODE_CTRL;
	if(mods & (MOD_LALT | MOD_RALT))
		mods |= MODE_ALT;

	kb->mods = mods;
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
		case KEY_LEFTALT:  mods &= ~MOD_LALT;  break;
		case KEY_LEFTCTRL: mods &= ~MOD_LCTRL; break;
		case KEY_RIGHTALT: mods &= ~MOD_RALT;  break;
		case KEY_RIGHTCTRL:mods &= ~MOD_RCTRL; break;
		default: return;
	}

	set_mods(kb, mods);
}

static void key_press(struct device* kb, int code)
{
	int mask = MODE_CTRL | MODE_ALT;
	struct action* ka;

	for(ka = actions; ka < actions + nactions; ka++) {
		if(ka->code != code)
			continue;
		if((ka->mode & mask) != (kb->mods & mask))
			continue;

		if(ka->mode & MODE_HOLD)
			start_hold(ka, kb);
		else
			spawn(ka);
	}

	int mods = kb->mods;

	switch(code) {
		case KEY_LEFTALT:  mods |= MOD_LALT;  break;
		case KEY_LEFTCTRL: mods |= MOD_LCTRL; break;
		case KEY_RIGHTALT: mods |= MOD_RALT;  break;
		case KEY_RIGHTCTRL:mods |= MOD_RCTRL; break;
		default: return;
	}

	set_mods(kb, mods);
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
