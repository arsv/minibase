#include <sys/fork.h>
#include <sys/_exit.h>
#include <sys/chdir.h>
#include <sys/chroot.h>
#include <sys/open.h>
#include <sys/close.h>
#include <sys/dup2.h>
#include <sys/write.h>
#include <sys/waitpid.h>
#include <sys/setresuid.h>
#include <sys/setresgid.h>
#include <sys/prlimit.h>
#include <sys/getpid.h>
#include <sys/seccomp.h>
#include <sys/setgroups.h>
#include <sys/setpriority.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"

static int numargs(struct sh* ctx, int argc, int min, int max)
{
	if(min && argc < min)
		return error(ctx, "too few arguments", NULL, 0);
	if(max && argc > max)
		return error(ctx, "too many arguments", NULL, 0);
	return 0;
}

static int argint(struct sh* ctx, char* arg, int* dst)
{
	char* p;

	if(!(p = parseint(arg, dst)) || *p)
		return error(ctx, "numeric argument rquired", arg, 0);

	return 0;
}

static int argu64(struct sh* ctx, char* arg, uint64_t* dst)
{
	char* p;

	if(!(p = parseu64(arg, dst)) || *p)
		return error(ctx, "numeric argument rquired", arg, 0);

	return 0;
}

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
	int i, ret;

	int pid = sysgetpid();
	char pbuf[20];
	char* p = fmtint(pbuf, pbuf + sizeof(pbuf) - 1, pid); *p = '\0';
	int plen = p - pbuf;

	if((ret = numargs(ctx, argc, 2, 0)))
		return ret;

	for(i = 1; i < argc; i++)
		if((ret = setcg(ctx, base, argv[i], pbuf, plen)))
			break;

	return ret;
}

static int cmd_seccomp(struct sh* ctx, int argc, char** argv)
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

static int cmd_setuid(struct sh* ctx, int argc, char** argv)
{
	int ret, uid;
	char* pwfile = "/etc/passwd";

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;
	if((ret = pwresolve(ctx, pwfile, 1, &argv[1], &uid, "unknown user")))
		return ret;

	return fchk(sys_setresuid(uid, uid, uid), ctx, "setuid", argv[1]);
}

static int cmd_setgid(struct sh* ctx, int argc, char** argv)
{
	int ret, gid;
	char* pwfile = "/etc/group";

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;
	if((ret = pwresolve(ctx, pwfile, 1, &argv[1], &gid, "unknown group")))
		return ret;

	return fchk(sys_setresgid(gid, gid, gid), ctx, "setgid", argv[1]);
}

static int cmd_groups(struct sh* ctx, int argc, char** argv)
{
	char* pwfile = "/etc/group";
	int ng = argc - 1;
	int grp[ng];
	int ret;

	if((ret = numargs(ctx, argc, 2, 0)))
		return ret;
	if((ret = pwresolve(ctx, pwfile, ng, &argv[1], grp, "unknown group")))
		return ret;

	return fchk(sys_setgroups(ng, grp), ctx, "setgroups", NULL);
}

static int openflags(struct sh* ctx, int* dst, char* str)
{
	int flags = 0;
	char* p;

	for(p = str; *p; p++) switch(*p) {
		case 'w': flags |= O_WRONLY; break;
		case 'r': flags |= O_RDONLY; break;
		case 'a': flags |= O_APPEND; break;
		case 'c': flags |= O_CREAT; break;
		default: return error(ctx, "open", "unknown flags", 0);
	}

	*dst = flags;
	return 0;
}

static int cmd_open(struct sh* ctx, int argc, char** argv)
{
	int fd, rfd, ret, i = 1;
	int flags = O_RDWR;

	if(i < argc && argv[i][0] == '-')
		flags = openflags(ctx, &flags, argv[i++] + 1);

	char* srfd = argv[i++];
	char* name = argv[i];

	if((ret = numargs(ctx, argc - i, 2, 2)))
		return ret;
	if((ret = argint(ctx, srfd, &rfd)))
		return ret;

	if((fd = sysopen3(name, flags, 0666)) < 0)
		return error(ctx, "open", name, fd);
	if(fd == rfd)
		return 0;

	ret = fchk(sysdup2(fd, rfd), ctx, "dup", srfd);
	sysclose(fd);

	return ret;
}

static int cmd_dupfd(struct sh* ctx, int argc, char** argv)
{
	int ret, oldfd, newfd;

	if((ret = numargs(ctx, argc, 3, 3)))
		return ret;
	if((ret = argint(ctx, argv[1], &oldfd)))
		return ret;
	if((ret = argint(ctx, argv[2], &newfd)))
		return ret;

	return fchk(sysdup2(oldfd, newfd), ctx, "dup", argv[1]);
}

static int cmd_close(struct sh* ctx, int argc, char** argv)
{
	int ret, fd;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;
	if((ret = argint(ctx, argv[1], &fd)))
		return ret;

	return fchk(sysclose(fd), ctx, "close", argv[1]);
}

static int cmd_setprio(struct sh* ctx, int argc, char** argv)
{
	int prio;
	int ret;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;
	if((ret = argint(ctx, argv[1], &prio)))
		return ret;

	return fchk(sys_setpriority(0, 0, prio), ctx, "setpriority", argv[1]);
}

static int cmd_exec(struct sh* ctx, int argc, char** argv)
{
	int ret;

	if((ret = numargs(ctx, argc, 2, 0)))
		return ret;

	return fchk(execvpe(argv[1], argv+1, ctx->envp), ctx, "exec", argv[1]);
}

static int cmd_unset(struct sh* ctx, int argc, char** argv)
{
	int i;
	int ret;

	if((ret = numargs(ctx, argc, 2, 0)))
		return ret;

	for(i = 1; i < argc; i++)
		undef(ctx, argv[i]);

	return 0;
}

static int cmd_exit(struct sh* ctx, int argc, char** argv)
{
	int ret, code = 0;

	if((ret = numargs(ctx, argc, 1, 2)))
		return ret;
	if(argc > 1 && (ret = argint(ctx, argv[1], &code)))
		return ret;

	_exit(code);
}

static int cmd_chroot(struct sh* ctx, int argc, char** argv)
{
	int ret;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;

	return fchk(syschroot(argv[1]), ctx, "chroot", argv[1]);
}

static int cmd_cd(struct sh* ctx, int argc, char** argv)
{
	int ret;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;

	return fchk(syschdir(argv[1]), ctx, "chdir", argv[1]);
}

static const struct cmd {
	char name[8];
	int (*cmd)(struct sh* ctx, int argc, char** argv);
} builtins[] = {
	{ "cd",       cmd_cd      },
	{ "exit",     cmd_exit    },
	{ "exec",     cmd_exec    },
	{ "unset",    cmd_unset   },
	{ "open",     cmd_open    },
	{ "dupfd",    cmd_dupfd   },
	{ "close",    cmd_close   },
	{ "setuid",   cmd_setuid  },
	{ "setgid",   cmd_setgid  },
	{ "groups",   cmd_groups  },
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
