#include <sys/caps.h>
#include <sys/prctl.h>
#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

/* See capabilities(7).
   Preferably with some booze at hand, ain't gonna be easy. */

static const struct cap {
	char name[24];
	short val;
} caps[] = {
	{ "chown",               0 },
	{ "dac-override",        1 },
	{ "dac-read-search",     2 },
	{ "fowner",              3 },
	{ "fsetid",              4 },
	{ "kill",                5 },
	{ "setgid",              6 },
	{ "setuid",              7 },
	{ "setpcap",             8 },
	{ "linux-immutable",     9 },
	{ "net-bind-service",   10 },
	{ "net-broadcast",      11 },
	{ "net-admin",          12 },
	{ "net-raw",            13 },
	{ "ipc-lock",           14 },
	{ "ipc-owner",          15 },
	{ "sys-module",         16 },
	{ "sys-rawio",          17 },
	{ "sys-chroot",         18 },
	{ "sys-ptrace",         19 },
	{ "sys-pacct",          20 },
	{ "sys-admin",          21 },
	{ "sys-boot",           22 },
	{ "sys-nice",           23 },
	{ "sys-resource",       24 },
	{ "sys-time",           25 },
	{ "sys-tty-config",     26 },
	{ "mknod",              27 },
	{ "lease",              28 },
	{ "audit-write",        29 },
	{ "audit-control",      30 },
	{ "setfcap",            31 },
	{ "mac-override",       32 },
	{ "mac-admin",          33 },
	{ "syslog",             34 },
	{ "wake-alarm",         35 },
	{ "block-suspend",      36 },
	{ "audit-read",         37 }
};

#define OPT_a (1<<0)
#define OPT_b (1<<1)
#define OPT_i (1<<2)
#define OPT_p (1<<3)
#define OPT_e (1<<4)

static void parsecaps(CTX, int* bits)
{
	const struct cap* sc;
	char* arg;

	need_some_arguments(ctx);

	while((arg = next(ctx))) {
		for(sc = caps; sc < ARRAY_END(caps); sc++)
			if(!strncmp(sc->name, arg, sizeof(sc->name)))
				break;
		if(sc >= ARRAY_END(caps))
			fatal(ctx, "unknown cap", arg);

		int idx = (sc->val / 32);
		int bit = (sc->val & 31);

		bits[idx] |= (1 << bit);
	}
}

static void setbcaps(CTX, int* bits, int opts)
{
	const struct cap* sc;
	int ret;

	if(!(opts & OPT_b))
		return;

	for(sc = caps; sc->name[0]; sc++) {
		int cap = sc->val;
		int idx = cap / 32;
		int bit = cap % 32;

		if(bits[idx] & (1 << bit))
			continue;
		if((ret = sys_prctl(PR_CAPBSET_DROP, cap, 0, 0, 0)) < 0)
			error(ctx, "prctl", "PR_CAPBSET_DROP", ret);
	}
}

static int pr_cap_ambient(CTX, int op, int cap)
{
	int ret;

	if((ret = sys_prctl(PR_CAP_AMBIENT, op, cap, 0, 0)) < 0)
		error(ctx, "prctl", "PR_CAP_AMBIENT", ret);

	return ret;
}

static void pr_cap_ambient_clear_all(CTX)
{
	(void)pr_cap_ambient(ctx, PR_CAP_AMBIENT_CLEAR_ALL, 0);
}

static int pr_cap_ambient_is_set(CTX, int cap)
{
	return pr_cap_ambient(ctx, PR_CAP_AMBIENT_IS_SET, cap);
}

static void setacaps(CTX, int* bits, int opts)
{
	const struct cap* sc;

	if(!(opts & OPT_a))
		return;
	if(!bits[0] && !bits[1])
		return pr_cap_ambient_clear_all(ctx);

	for(sc = caps; sc->name[0]; sc++) {
		int cap = sc->val;
		int idx = cap / 32;
		int bit = cap % 32;

		int need = (bits[idx] & (1 << bit));

		int ret = pr_cap_ambient_is_set(ctx, cap);

		if((!ret && !need) || (ret && need))
			continue;

		int op = need ? PR_CAP_AMBIENT_RAISE : PR_CAP_AMBIENT_LOWER;

		(void)pr_cap_ambient(ctx, op, cap);
	}
}

static void setepicaps(CTX, int* bits, int opts, struct cap_data* cd)
{
	struct cap_header ch = {
		.version = LINUX_CAPABILITY_VERSION,
		.pid = 0
	};

	if(opts & OPT_p) {
		cd[0].permitted = bits[0];
		cd[1].permitted = bits[1];
	} if(opts & (OPT_i | OPT_a)) {
		cd[0].inheritable = bits[0];
		cd[1].inheritable = bits[1];
	} if(opts & OPT_e) {
		cd[0].effective = bits[0];
		cd[1].effective = bits[1];
	} else {
		cd[0].effective &= cd[0].permitted;
		cd[1].effective &= cd[1].permitted;
	}

	int ret = sys_capset(&ch, cd);

	check(ctx, "capset", NULL, ret);
}

static void prepcaps(CTX, int* caps, int opts, struct cap_data* cd)
{
	int ret;
	struct cap_header ch = {
		.version = LINUX_CAPABILITY_VERSION,
		.pid = 0
	};

	if((ret = sys_capget(&ch, cd)) < 0)
		error(ctx, "capget", NULL, ret);

	if(cd[0].effective == cd[0].permitted)
		return;

	cd[0].effective = cd[0].permitted;
	cd[1].effective = cd[1].permitted;

	if(opts & OPT_a) {
		cd[0].inheritable |= caps[0];
		cd[1].inheritable |= caps[1];
	}

	if((ret = (sys_capset(&ch, cd))) < 0)
		error(ctx, "capset", NULL, ret);
}

static int capopts(CTX, char* arg)
{
	int opts = 0;
	char* p;

	for(p = arg; *p; p++) {
		char c = *p;

		if(c == 'a') opts |= OPT_a;
		else if(c == 'b') opts |= OPT_b;
		else if(c == 'i') opts |= OPT_i;
		else if(c == 'p') opts |= OPT_p;
		else if(c == 'e') opts |= OPT_e;
		else fatal(ctx, "bad mode", arg);
	}

	return opts;
}

void cmd_setcaps(CTX)
{
	struct cap_data dh[2];
	int caps[2] = { 0, 0 };
	char* optstr;
	int opts;

	if((optstr = dash_opts(ctx)))
		opts = capopts(ctx, optstr);
	else
		opts = OPT_a | OPT_b | OPT_p | OPT_i;

	parsecaps(ctx, caps);

	prepcaps(ctx, caps, opts, dh);

	setbcaps(ctx, caps, opts);

	setacaps(ctx, caps, opts);

	setepicaps(ctx, caps, opts, dh);
}
