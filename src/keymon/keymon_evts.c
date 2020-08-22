#include <bits/major.h>
#include <bits/input.h>
#include <bits/input/key.h>

#include <sys/file.h>
#include <sys/signal.h>
#include <sys/ppoll.h>
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

void check_children(CTX)
{
	struct act* ka;
	int pid, status;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
		for(ka = first(ctx); ka; ka = next(ctx, ka))
			if(ka->pid == pid)
				ka->pid = 0;
	}
}

static void prep_argv(struct act* ka, char* argv[], int max)
{
	int i = 0, n = max - 1;

	char* p = ka->cmd;
	char* e = p + (ka->len - sizeof(*ka));

	while(p < e) {
		char* q = p;

		while(q < e && *q) q++;

		if(q >= e) break;

		if(i >= n) break;

		argv[i++] = p;

		p = q + 1;
	}

	argv[i++] = NULL;
}

static void spawn_action(CTX, struct act* ka)
{
	int pid, ret;
	char* argv[5];

	if((pid = ka->pid) > 0) {
		sys_kill(pid, SIGTERM);
		return;
	}

	prep_argv(ka, argv, ARRAY_SIZE(argv));

	FMTBUF(p, e, path, 200);
	p = fmtstr(p, e, CONFDIR "/");
	p = fmtstr(p, e, argv[0]);
	FMTEND(p, e);

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return;
	} else if(pid == 0) {
		ret = execvpe(path, argv, ctx->environ);
		fail("exec", path, ret);
	}

	ka->pid = pid;
}

void hold_timeout(CTX, struct act* ka)
{
	ctx->held = NULL;

	spawn_action(ctx, ka);
}

static void start_hold(CTX, struct act* ka)
{
	int sec;

	if(ka->mode & MODE_LONG)
		sec = LONGTIME;
	else
		sec = HOLDTIME;

	ctx->ts.sec = sec;
	ctx->ts.nsec = 0;

	ctx->held = ka;
}

static void set_global_mods(CTX)
{
	byte* bits = ctx->bits;
	int i, nbits = ctx->nbits;
	int modstate = 0;

	for(i = 0; i < nbits; i++) {
		byte devmods = bits[i];

		if(devmods & KEYM_LCTRL)
			modstate |= MODE_CTRL;
		if(devmods & KEYM_LALT)
			modstate |= MODE_ALT;
	}

	ctx->modstate = modstate;
}

static void key_release(CTX, int code, byte* mods)
{
	struct act* ka = ctx->held;

	if(ka && ka->code == code)
		ctx->held = 0;

	int curr = *mods;

	switch(code) {
		case KEY_LEFTALT:  curr &= ~KEYM_LALT;  break;
		case KEY_LEFTCTRL: curr &= ~KEYM_LCTRL; break;
		case KEY_RIGHTALT: curr &= ~KEYM_RALT;  break;
		case KEY_RIGHTCTRL:curr &= ~KEYM_RCTRL; break;
		default: return;
	}

	*mods = curr;

	set_global_mods(ctx);
}

static void key_press(CTX, int code, byte* mods)
{
	int mask = MODE_CTRL | MODE_ALT;
	struct act* ka;

	for(ka = first(ctx); ka; ka = next(ctx, ka)) {
		if(ka->code != code)
			continue;
		if((ka->mode & mask) != (ctx->modstate & mask))
			continue;

		if(ka->mode & MODE_HOLD)
			start_hold(ctx, ka);
		else
			spawn_action(ctx, ka);
	}

	int curr = *mods;

	switch(code) {
		case KEY_LEFTALT:  curr |= KEYM_LALT;  break;
		case KEY_LEFTCTRL: curr |= KEYM_LCTRL; break;
		case KEY_RIGHTALT: curr |= KEYM_RALT;  break;
		case KEY_RIGHTCTRL:curr |= KEYM_RCTRL; break;
		default: return;
	}

	*mods = curr;

	set_global_mods(ctx);
}

static void handle_event(CTX, struct event* ev, byte* mods)
{
	int value = ev->value;
	int type = ev->type;
	int code = ev->code;

	if(type == EV_KEY) {
		if(value == 1)
			key_press(ctx, code, mods);
		else if(!value)
			key_release(ctx, code, mods);
	}
	if(type == EV_SW) {
		code |= CODE_SWITCH;

		if(value == 1)
			key_press(ctx, code, mods);
		else if(!value)
			key_release(ctx, code, mods);
	}
}

void handle_input(CTX, int fd, byte* mods)
{
	char buf[256];
	int rd;

	while((rd = sys_read(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct event* ev = ptr;

			ptr += sizeof(ev);

			handle_event(ctx, ev, mods);
		}
	}
}
