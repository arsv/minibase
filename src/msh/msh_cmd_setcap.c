#include <sys/caps.h>
#include <sys/prctl.h>
#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

/* Ref. capabilities(7).
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
	{ "audit-read",         37 },
	{ "",                   -1 }
};

#define OPT_a (1<<0)
#define OPT_b (1<<1)
#define OPT_i (1<<2)
#define OPT_p (1<<3)
#define OPT_e (1<<4)

static int parsecaps(struct sh* ctx, int argn, char** args, int* bits)
{
	const struct cap* sc;
	int i;

	for(i = 0; i < argn; i++) {
		for(sc = caps; sc->name[0]; sc++)
			if(!strncmp(sc->name, args[i], sizeof(sc->name)))
				break;
		if(!sc->name[0])
			return error(ctx, "unknown cap", args[i], 0);

		int idx = (sc->val / 32);
		int bit = (sc->val & 31);

		bits[idx] |= (1 << bit);
	}
	
	return 0;
}

static int setbcaps(struct sh* ctx, int* bits, int opts)
{
	const struct cap* sc;
	int ret;

	if(!(opts & OPT_b))
		return 0;

	for(sc = caps; sc->name[0]; sc++) {
		int cap = sc->val;
		int idx = cap / 32;
		int bit = cap % 32;

		if(bits[idx] & (1 << bit))
			continue;
		if((ret = sys_prctl(PR_CAPBSET_DROP, cap, 0, 0, 0)) < 0)
			return error(ctx, "prctl", "PR_CAPBSET_DROP", ret);
	}
	
	return 0;
}

static int pr_cap_ambient(struct sh* ctx, int op, int cap)
{
	int ret;
	
	if((ret = sys_prctl(PR_CAP_AMBIENT, op, cap, 0, 0)) < 0)
		return error(ctx, "prctl", "PR_CAP_AMBIENT", ret);
	
	return ret;
}

static int pr_cap_ambient_clear_all(struct sh* ctx)
{
	int ret;

	if((ret = pr_cap_ambient(ctx, PR_CAP_AMBIENT_CLEAR_ALL, 0)) < 0)
		return ret;

	return 0;
}

static int pr_cap_ambient_is_set(struct sh* ctx, int cap)
{
	return pr_cap_ambient(ctx, PR_CAP_AMBIENT_IS_SET, cap);
}

static int setacaps(struct sh* ctx, int* bits, int opts)
{
	const struct cap* sc;

	if(!(opts & OPT_a))
		return 0;
	if(!bits[0] && !bits[1])
		return pr_cap_ambient_clear_all(ctx);

	for(sc = caps; sc->name[0]; sc++) {
		int cap = sc->val;
		int idx = cap / 32;
		int bit = cap % 32;

		int ret, need = (bits[idx] & (1 << bit));

		if((ret = pr_cap_ambient_is_set(ctx, cap)) < 0)
			return ret;
		if((!ret && !need) || (ret && need))
			continue;

		int op = need ? PR_CAP_AMBIENT_RAISE : PR_CAP_AMBIENT_LOWER;

		if((ret = pr_cap_ambient(ctx, op, cap)) < 0)
			return ret;
	}
	
	return 0;
}

static int setepicaps(struct sh* ctx, int* bits, int opts)
{
	int ret;
	struct cap_data cd[2];
	struct cap_header ch = {
		.version = LINUX_CAPABILITY_VERSION,
		.pid = 0
	};

	if(opts & (OPT_p | OPT_i | OPT_e))
		memset(&cd, 0, sizeof(cd));
	else if((ret = fchk(sys_capget(&ch, cd), ctx, "capget", NULL)))
		return ret;

	if(opts & OPT_p) {
		cd[0].permitted = bits[0];
		cd[1].permitted = bits[1];
	} if(opts & OPT_i) {
		cd[0].inheritable = bits[0];
		cd[1].inheritable = bits[1];
	} if(opts & OPT_e) {
		cd[0].effective = bits[0];
		cd[1].effective = bits[1];
	}

	return fchk(sys_capset(&ch, cd), ctx, "capset", NULL);
}

static int capopts(struct sh* ctx, char* arg)
{
	int opts = 0;
	char* p;

	for(p = arg; *p; p++)
		switch(*p) {
			case 'a': opts |= OPT_a; break;
			case 'b': opts |= OPT_b; break;
			case 'i': opts |= OPT_i; break;
			case 'p': opts |= OPT_p; break;
			case 'e': opts |= OPT_e; break;
			default: return error(ctx, "bad mode", arg, 0);
		}

	return opts;
}

int cmd_setcaps(struct sh* ctx, int argc, char** argv)
{
	int caps[2] = { 0, 0 };
	int opts, i = 1;
	int ret;

	if(i < argc && argv[i][0] == '-')
		opts = capopts(ctx, argv[i++] + 1);
	else
		opts = OPT_a | OPT_b | OPT_p | OPT_i;

	if((ret = parsecaps(ctx, argc - i, argv + i, caps)))
		return ret;

	if((ret = setepicaps(ctx, caps, opts)))
		return ret;
	if((ret = setbcaps(ctx, caps, opts)))
		return ret;
	if((ret = setacaps(ctx, caps, opts)))
		return ret;

	return 0;
}
