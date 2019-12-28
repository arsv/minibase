#include <bits/stdio.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/creds.h>
#include <sys/sched.h>
#include <sys/rlimit.h>
#include <sys/seccomp.h>
#include <sys/mman.h>

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

void cmd_rlimit(CTX)
{
	const struct rlpair* rp;
	struct rlimit rl;

	char* key = shift(ctx);

	shift_u64(ctx, &rl.cur);

	if(got_more_arguments(ctx))
		shift_u64(ctx, &rl.max);
	else
		rl.max = rl.cur;

	no_more_arguments(ctx);

	for(rp = rlimits; rp < ARRAY_END(rlimits); rp++)
		if(!strcmp(rp->name, key))
			break;
	if(rp >= ARRAY_END(rlimits))
		fatal(ctx, "unknown limit", key);

	int ret = sys_prlimit(0, rp->res, &rl, NULL);

	check(ctx, "rlimit", key, ret);
}

void cmd_setprio(CTX)
{
	int prio;

	shift_int(ctx, &prio);
	no_more_arguments(ctx);

	int ret = sys_setpriority(0, 0, prio);

	check(ctx, "setpriority", NULL, ret);
}

void cmd_setcpus(CTX)
{
	int id;
	struct cpuset mask;

	memzero(&mask, sizeof(mask));
next:
	shift_int(ctx, &id);
	cpuset_set(&mask, id);

	if(got_more_arguments(ctx))
		goto next;

	int ret = sys_sched_setaffinity(0, &mask);

	check(ctx, "sched_setaffinity", NULL, ret);
}

void cmd_umask(CTX)
{
	int mask;

	shift_oct(ctx, &mask);
	no_more_arguments(ctx);

	int ret = sys_umask(mask);

	check(ctx, "umask", NULL, ret);
}

void cmd_chroot(CTX)
{
	char* dir = shift(ctx);

	no_more_arguments(ctx);

	int ret = sys_chroot(dir);

	check(ctx, "chroot", NULL, ret);
}

/* UID-GID commands */

static int resolve_name(struct mbuf* mb, char* name)
{
	char* filedata = mb->buf;
	char* fileend = filedata + mb->len;
	int id;

	/* user:x:500:...\n */
	/* ls  ue un     le */
	char *ls, *le;
	char *ue, *un;
	char *ne = NULL;
	for(ls = filedata; ls < fileend; ls = le + 1) {
		le = strecbrk(ls, fileend, '\n');
		ue = strecbrk(ls, le, ':');
		if(ue >= le) continue;
		un = strecbrk(ue + 1, le, ':') + 1;
		if(un >= le) continue;

		if(strncmp(name, ls, ue - ls))
			continue;

		ne = parseint(un, &id);
		break;
	};

	if(!ne || *ne != ':')
		return -1;

	return id;
}

static int parse_int_id(char* name)
{
	int id;
	char* p;

	if(!(p = parseint(name, &id)) || *p)
		return -1;

	return id;
}

int get_user_id(CTX, char* user)
{
	struct mbuf* mb = &ctx->passwd;
	int id;

	if((id = parse_int_id(user)) >= 0)
		return id;
	if(!mb->buf)
		map_file(ctx, mb, "/etc/passwd");
	if((id = resolve_name(mb, user)) >= 0)
		return id;

	fatal(ctx, "unknown group", user);
}

int get_group_id(CTX, char* group)
{
	struct mbuf* mb = &ctx->groups;
	int id;

	if((id = parse_int_id(group)) >= 0)
		return id;
	if(!mb->buf)
		map_file(ctx, mb, "/etc/group");
	if((id = resolve_name(mb, group)) >= 0)
		return id;

	fatal(ctx, "unknown group", group);
}

void cmd_setuid(CTX)
{
	char* user = shift(ctx);

	no_more_arguments(ctx);

	int uid = get_user_id(ctx, user);

	int ret = sys_setresuid(uid, uid, uid);

	check(ctx, "setresuid", NULL, ret);
}

void cmd_setgid(CTX)
{
	char* group = shift(ctx);

	no_more_arguments(ctx);

	int gid = get_group_id(ctx, group);

	int ret = sys_setresgid(gid, gid, gid);

	check(ctx, "setresgid", NULL, ret);
}

void cmd_groups(CTX)
{
	int gids[32];
	int i, n = ARRAY_SIZE(gids);
	char* group;

	need_some_arguments(ctx);

	while((group = next(ctx))) {
		if(i >= n) fatal(ctx, "too many groups", NULL);
		gids[i++] = get_group_id(ctx, group);
	}

	int ret = sys_setgroups(i, gids);

	check(ctx, "setgroups", NULL, ret);
}
