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
#include "msh_cmd.h"

struct cmd {
	char name[12];
	int (*cmd)(struct sh* ctx, int argc, char** argv);
};

static const struct cmd ifelses[] = {
	{ "if",       cmd_if   },
	{ "else",     cmd_else },
	{ "elif",     cmd_elif },
	{ "fi",       cmd_fi   },
	{ "",         NULL     }
};

static const struct cmd builtin[] = {
	{ "cd",       cmd_cd      },
	{ "exit",     cmd_exit    },
	{ "exec",     cmd_exec    },
	{ "echo",     cmd_echo    },
	{ "warn",     cmd_warn    },
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

/* Top-level commands stuff */

static const struct cmd* findcmd(const struct cmd table[], char** argv)
{
	const struct cmd* cc;

	for(cc = table; cc->cmd; cc++)
		if(!strcmp(cc->name, argv[0]))
			return cc;
	
	return NULL;
}

static int leadingdash(char** argv)
{
	int ret = (argv[0][0] == '-');

	if(ret) argv[0]++;

	return ret;
}

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

void command(struct sh* ctx, int argc, char** argv)
{
	const struct cmd* bi;
	int ret;

	if((bi = findcmd(ifelses, argv)))
		if(bi->cmd(ctx, argc, argv))
			_exit(0xFF);
	if(bi || ctx->cond & CSKIP)
		return;

	int noerror = leadingdash(argv);

	if((bi = findcmd(builtin, argv)))
		ret = bi->cmd(ctx, argc, argv);
	else
		ret = spawn(ctx, argc, argv);

	if(ret && !noerror)
		fatal(ctx, "command failed", NULL);
}
