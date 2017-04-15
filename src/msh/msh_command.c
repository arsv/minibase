#include <sys/fork.h>
#include <sys/_exit.h>
#include <sys/chdir.h>
#include <sys/chroot.h>
#include <sys/waitpid.h>
#include <sys/setresuid.h>
#include <sys/setresgid.h>
#include <sys/prlimit.h>
#include <sys/setpriority.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"

#define NLEN 11

struct rlpair {
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

static int cmd_rlimit(struct sh* ctx, int argc, char** argv)
{
	char* p;
	struct rlpair* rp;
	struct rlimit rl;

	if(argc < 3)
		return error(ctx, "too few arguments", NULL, 0);
	if(argc > 4)
		return error(ctx, "too many arguments", NULL, 0);

	for(rp = rlimits; rp->name[0]; rp++)
		if(!strcmp(rp->name, argv[1]))
			break;
	if(!rp->name[0])
		return error(ctx, "unknown limit", argv[1], 0);

	if(!(p = parseu64(argv[2], &rl.cur)) || *p)
		return error(ctx, "invalid value", argv[2], 0);
	if(argc < 4)
		rl.max = rl.cur;
	else if(!(p = parseu64(argv[3], &rl.max)) || *p)
		return error(ctx, "invalid value", argv[3], 0);

	return fchk(sys_prlimit(0, rp->res, &rl, NULL), ctx, "fchk", argv[1]);
}

static int cmd_setuid(struct sh* ctx, int argc, char** argv)
{
	int ret, uid;
	char* pwfile = "/etc/passwd";

	if(argc != 2)
		return error(ctx, "single argument required", NULL, 0);

	if((ret = pwresolve(ctx, pwfile, 1, &argv[1], &uid, "unknown user")))
		return ret;

	if((ret = sys_setresuid(uid, uid, uid)) < 0)
		return error(ctx, "setuid", argv[1], ret);

	return 0;
}

static int cmd_setgid(struct sh* ctx, int argc, char** argv)
{
	int ret, gid;
	char* pwfile = "/etc/group";

	if(argc != 2)
		return error(ctx, "single argument required", NULL, 0);

	if((ret = pwresolve(ctx, pwfile, 1, &argv[1], &gid, "unknown group")))
		return ret;

	if((ret = sys_setresgid(gid, gid, gid)) < 0)
		return error(ctx, "setgid", argv[1], ret);

	return 0;
}

static int cmd_cd(struct sh* ctx, int argc, char** argv)
{
	return fchk(syschdir(argv[1]), ctx, "chdir", argv[1]);
}

static int cmd_chroot(struct sh* ctx, int argc, char** argv)
{
	return fchk(syschroot(argv[1]), ctx, "chroot", argv[1]);
}

static int cmd_setprio(struct sh* ctx, int argc, char** argv)
{
	char* p;
	int prio;

	if(argc != 2)
		return error(ctx, "single argument required", NULL, 0);
	if((p = parseint(argv[1], &prio)) || *p)
		return error(ctx, "argument must be numeric", NULL, 0);

	return fchk(sys_setpriority(0, 0, prio), ctx, "setpriority", argv[1]);
}

static int cmd_exit(struct sh* ctx, int argc, char** argv)
{
	_exit(0);
}

static int cmd_exec(struct sh* ctx, int argc, char** argv)
{
	if(argc < 2)
		return error(ctx, "exec:", "too few arguments", 0);

	long ret = execvpe(argv[1], argv+1, ctx->envp);

	return error(ctx, "exec", argv[1], ret);
}

static int cmd_unset(struct sh* ctx, int argc, char** argv)
{
	int i;

	if(argc < 2)
		return error(ctx, "unset:", "too few arguments", 0);

	for(i = 1; i < argc; i++)
		undef(ctx, argv[i]);

	return 0;
}

static const struct cmd {
	char name[NLEN];
	int (*cmd)(struct sh* ctx, int argc, char** argv);
} builtins[] = {
	{ "cd",       cmd_cd      },
	{ "exit",     cmd_exit    },
	{ "exec",     cmd_exec    },
	{ "unset",    cmd_unset   },
	{ "setuid",   cmd_setuid  },
	{ "setgid",   cmd_setgid  },
	{ "chroot",   cmd_chroot  },
	{ "setprio",  cmd_setprio },
	{ "rlimit",   cmd_rlimit  },
	{ "",         NULL        }
};

static int spawn(struct sh* ctx, int argc, char** argv)
{
	long pid = sysfork();
	int status;

	if(pid < 0)
		fail("fork", NULL, pid);

	if(!pid) {
		long ret = execvpe(*argv, argv, ctx->envp);
		error(ctx, "exec", *argv, ret);
		_exit(0xFF);
	}

	if((pid = syswaitpid(pid, &status, 0)) < 0)
		fail("wait", *argv, pid);

	return status;
}

void exec(struct sh* ctx, int argc, char** argv)
{
	const struct cmd* bi;
	int noerror = 0;
	int ret;

	if(argv[0][0] == '-') {
		noerror = 1;
		argv[0]++;
	}

	for(bi = builtins; bi->cmd; bi++)
		if(!strcmp(bi->name, argv[0]))
			break;
	if(bi->cmd)
		ret = bi->cmd(ctx, argc, argv);
	else
		ret = spawn(ctx, argc, argv);

	if(!ret || noerror)
		return;

	fatal(ctx, "command failed", NULL);
}
