#include <bits/stdio.h>

#include <sys/fpath.h>
#include <sys/creds.h>
#include <sys/sched.h>
#include <sys/rlimit.h>
#include <sys/seccomp.h>

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
	{ "stack",    RLIMIT_STACK      }
};

int cmd_rlimit(CTX)
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

	for(rp = rlimits; rp < ARRAY_END(rlimits); rp++)
		if(!strcmp(rp->name, key))
			break;
	if(rp >= ARRAY_END(rlimits))
		return error(ctx, "unknown limit", key, 0);

	return fchk(sys_prlimit(0, rp->res, &rl, NULL), ctx, key);
}

int cmd_seccomp(CTX)
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

int cmd_setprio(CTX)
{
	int prio;

	if(shift_int(ctx, &prio))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sys_setpriority(0, 0, prio), ctx, NULL);
}

int cmd_umask(CTX)
{
	int mask;

	if(shift_oct(ctx, &mask))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sys_umask(mask), ctx, NULL);
}

int cmd_chroot(CTX)
{
	char* dir;

	if(shift_str(ctx, &dir))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sys_chroot(dir), ctx, dir);
}

int cmd_setuid(CTX)
{
	int uid;
	char* user;

	if(shift_str(ctx, &user))
		return -1;
	if(moreleft(ctx))
		return -1;
	if(get_user_id(ctx, user, &uid))
		return -1;

	return fchk(sys_setresuid(uid, uid, uid), ctx, user);
}

int cmd_setgid(CTX)
{
	int gid;
	char* group;

	if(shift_str(ctx, &group))
		return -1;
	if(moreleft(ctx))
		return -1;
	if(get_group_id(ctx, group, &gid))
		return -1;

	return fchk(sys_setresgid(gid, gid, gid), ctx, group);
}

int cmd_groups(CTX)
{
	int num = numleft(ctx);
	char** groups = argsleft(ctx);
	int gids[num];

	if(noneleft(ctx))
		return -1;

	for(int i = 0; i < num; i++)
		if(get_group_id(ctx, groups[i], &gids[i]))
			return -1;

	return fchk(sys_setgroups(num, gids), ctx, NULL);
}
