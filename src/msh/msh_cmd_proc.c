#include <sys/_exit.h>
#include <sys/umask.h>
#include <sys/chdir.h>
#include <sys/chroot.h>
#include <sys/prlimit.h>
#include <sys/seccomp.h>
#include <sys/setpriority.h>

#include <string.h>
#include <format.h>
#include <util.h>

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

int cmd_rlimit(struct sh* ctx, int argc, char** argv)
{
	int ret;
	const struct rlpair* rp;
	struct rlimit rl;

	if((ret = numargs(ctx, argc, 3, 4)))
		return ret;

	for(rp = rlimits; rp->name[0]; rp++)
		if(!strcmp(rp->name, argv[1]))
			break;
	if(!rp->name[0])
		return error(ctx, "unknown limit", argv[1], 0);

	if((ret = argu64(ctx, argv[2], &rl.cur)))
		return ret;
	if(argc < 4)
		rl.max = rl.cur;
	else if((ret = argu64(ctx, argv[3], &rl.max)))
		return ret;

	return fchk(sys_prlimit(0, rp->res, &rl, NULL), ctx, "fchk", argv[1]);
}

int cmd_seccomp(struct sh* ctx, int argc, char** argv)
{
	struct mbuf mb;
	int ret;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;
	if((ret = fchk(mmapfile(&mb, argv[1]), ctx, "mmap", argv[1])))
		return ret;
	if(!mb.len || mb.len % 8) {
		ret = error(ctx, "odd size:", argv[1], 0);
		goto out;
	}

	struct seccomp sc = {
		.len = mb.len / 8,
		.buf = mb.buf
	};

	int mode = SECCOMP_SET_MODE_FILTER;
	ret = fchk(sys_seccomp(mode, 0, &sc), ctx, "seccomp", argv[1]);
out:
	munmapfile(&mb);
	return ret;
}

int cmd_setprio(struct sh* ctx, int argc, char** argv)
{
	int prio;
	int ret;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;
	if((ret = argint(ctx, argv[1], &prio)))
		return ret;

	return fchk(sys_setpriority(0, 0, prio), ctx, "setpriority", argv[1]);
}

int cmd_umask(struct sh* ctx, int argc, char** argv)
{
	int ret;
	int val;
	char* p;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;
	if(argv[1][0] != '0')
		return error(ctx, "non-octal mask", NULL, 0);
	if(!(p = parseoct(argv[1], &val)) || *p)
		return error(ctx, "invalid mask", NULL, 0);

	return fchk(sysumask(val), ctx, "umask", NULL);
}

int cmd_chroot(struct sh* ctx, int argc, char** argv)
{
	int ret;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;

	return fchk(syschroot(argv[1]), ctx, "chroot", argv[1]);
}
