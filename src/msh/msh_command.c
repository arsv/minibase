#include <sys/fork.h>
#include <sys/_exit.h>
#include <sys/chdir.h>
#include <sys/chroot.h>
#include <sys/open.h>
#include <sys/close.h>
#include <sys/write.h>
#include <sys/waitpid.h>
#include <sys/setresuid.h>
#include <sys/setresgid.h>
#include <sys/prlimit.h>
#include <sys/getpid.h>
#include <sys/seccomp.h>
#include <sys/setpriority.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"

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

static int cmd_rlimit(struct sh* ctx, int argc, char** argv)
{
	char* p;
	const struct rlpair* rp;
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

static int setcg(struct sh* ctx, char* base, char* grp, char* pbuf, int plen)
{
	int blen = strlen(base);
	int glen = strlen(grp);
	char path[blen+glen+20];
	int fd, wr;

	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtstr(p, e, base);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, grp);
	p = fmtstr(p, e, "/tasks");
	*p = '\0';

	if((fd = sysopen(path, O_WRONLY)) < 0)
		return fd;

	wr = syswrite(fd, pbuf, plen);

	sysclose(fd);

	return (wr == plen);
}

static int cmd_setcg(struct sh* ctx, int argc, char** argv)
{
	char* base = "/sys/fs/cgroup";
	int i = 1;
	int ret = 0;

	if(i < argc && argv[i][0] == '+')
		base = argv[i++] + 1;
	if(i >= argc)
		return error(ctx, "too few arguments", NULL, 0);

	int pid = sysgetpid();
	char pidbuf[20];
	char* p = pidbuf;
	char* e = pidbuf + sizeof(pidbuf) - 1;

	p = fmtint(p, e, pid);
	*p = '\0';

	for(; i < argc; i++)
		if((ret = setcg(ctx, base, argv[i], pidbuf, p - pidbuf)))
			break;

	return ret;
}

static int cmd_seccomp(struct sh* ctx, int argc, char** argv)
{
	struct mbuf mb;
	int ret;

	if(argc != 2)
		return error(ctx, "single argument required", NULL, 0);
	if((ret = mmapfile(&mb, argv[1])) < 0)
		return error(ctx, "cannot read", argv[1], ret);

	struct seccomp sc = {
		.len = mb.len / 8,
		.buf = mb.buf
	};

	if(!mb.len || mb.len % 8)
		ret = error(ctx, "odd size:", argv[1], 0);
	else if((ret = sys_seccomp(SECCOMP_SET_MODE_FILTER, 0, &sc)) < 0)
		ret = error(ctx, "seccomp", argv[1], ret);
	else
		ret = 0;

	munmapfile(&mb);

	return ret;
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

static int cmd_close(struct sh* ctx, int argc, char** argv)
{
	int fd;
	char* p;

	if(argc != 2)
		return error(ctx, "single argument required", NULL, 0);
	if(!(p = parseint(argv[1], &fd)) || *p)
		return error(ctx, "numeric argument required", NULL, 0);

	int ret = sysclose(fd);

	if(ret < 0 && ret != -EBADF)
		return error(ctx, "close", argv[1], ret);

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
	char name[8];
	int (*cmd)(struct sh* ctx, int argc, char** argv);
} builtins[] = {
	{ "cd",       cmd_cd      },
	{ "exit",     cmd_exit    },
	{ "exec",     cmd_exec    },
	{ "unset",    cmd_unset   },
	{ "close",    cmd_close   },
	{ "setuid",   cmd_setuid  },
	{ "setgid",   cmd_setgid  },
	{ "chroot",   cmd_chroot  },
	{ "setprio",  cmd_setprio },
	{ "rlimit",   cmd_rlimit  },
	{ "seccomp",  cmd_seccomp },
	{ "setcg" ,   cmd_setcg   },
	/* unshare is tricky */
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
