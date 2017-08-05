#include <bits/stdio.h>
#include <bits/ioctl/tty.h>

#include <sys/cwd.h>
#include <sys/umask.h>
#include <sys/rlimit.h>
#include <sys/seccomp.h>
#include <sys/pgrp.h>
#include <sys/ioctl.h>
#include <sys/priority.h>

#include <string.h>
#include <format.h>
#include <exit.h>

#include "msh.h"
#include "msh_cmd.h"

static const struct rlpair {
	char name[10];
	short res;
} rlimits[] = {
	{ "as",       RLIMIT_AS         },
	{ "core",     RLIMIT_CORE       },
	{ "cpu",      RLIMIT_CPU        },
	{ "data",     RLIMIT_DATA       },
	{ "fsize",    RLIMIT_FSIZE      },
	{ "locks",    RLIMIT_LOCKS      },
	{ "memlock",  RLIMIT_MEMLOCK    },
	{ "msgqueue", RLIMIT_MSGQUEUE   },
	{ "nice",     RLIMIT_NICE       },
	{ "nofile",   RLIMIT_NOFILE     },
	{ "nproc",    RLIMIT_NPROC      },
	{ "rss",      RLIMIT_RSS        },
	{ "rtprio",   RLIMIT_RTPRIO     },
	{ "rttime",   RLIMIT_RTTIME     },
	{ "sigpend",  RLIMIT_SIGPENDING },
	{ "stack",    RLIMIT_STACK      },
	{ "",         0                 }
};

int cmd_rlimit(struct sh* ctx)
{
	const struct rlpair* rp;
	struct rlimit rl;
	char* key;

	if(shift_str(ctx, &key))
		return -1;
	if(shift_u64(ctx, &rl.cur))
		return -1;
	if(!numleft(ctx))
		rl.max = rl.cur;
	else if(shift_u64(ctx, &rl.max))
		return -1;
	if(moreleft(ctx))
		return -1;

	for(rp = rlimits; rp->name[0]; rp++)
		if(!strcmp(rp->name, key))
			break;
	if(!rp->name[0])
		return error(ctx, "unknown limit", key, 0);

	return fchk(sys_prlimit(0, rp->res, &rl, NULL), ctx, key);
}

int cmd_seccomp(struct sh* ctx)
{
	struct mbuf mb;
	int ret;
	char* file;

	if(shift_str(ctx, &file))
		return -1;
	if(fchk(mmapfile(&mb, file), ctx, file))
		return -1;
	if(!mb.len || mb.len % 8) {
		ret = error(ctx, "odd size:", file, 0);
		goto out;
	}

	struct seccomp sc = {
		.len = mb.len / 8,
		.buf = mb.buf
	};

	int mode = SECCOMP_SET_MODE_FILTER;
	ret = fchk(sys_seccomp(mode, 0, &sc), ctx, file);
out:
	munmapfile(&mb);
	return ret;
}

int cmd_setprio(struct sh* ctx)
{
	int prio;

	if(shift_int(ctx, &prio))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sys_setpriority(0, 0, prio), ctx, NULL);
}

int cmd_umask(struct sh* ctx)
{
	int mask;

	if(shift_oct(ctx, &mask))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sys_umask(mask), ctx, NULL);
}

int cmd_chroot(struct sh* ctx)
{
	char* dir;

	if(shift_str(ctx, &dir))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sys_chroot(dir), ctx, dir);
}

int cmd_setsid(struct sh* ctx)
{
	int ret;

	if(moreleft(ctx))
		return -1;
	if((ret = sys_setsid()) < 0)
		return error(ctx, NULL, NULL, ret);
	if((ret = sys_ioctl(STDOUT, TIOCSCTTY, 0)) < 0)
		return error(ctx, "ioctl(TIOCSCTTY)", NULL, ret);

	return 0;
}
