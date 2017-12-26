#include <sys/proc.h>
#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/fpath.h>
#include <sys/prctl.h>
#include <sys/signal.h>

#include <sigset.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "cmd.h"

/* Exec and built-ins. The parser calls execute(argc,argv) and this
   code decides what to do with those.

   Everything here happens with the terminal already in canonical
   mode and the cursor at the start of an empty line. */

static void cmd_echo(CTX, int argc, char** argv)
{
	if(argc < 2)
		return warn("too few arguments", NULL, 0);
	if(argc > 2)
		return warn("too many arguments", NULL, 0);

	char* buf = argv[1];
	int len = strlen(buf);

	buf[len] = '\n';

	writeall(STDOUT, buf, len + 1);

	buf[len] = '\0';
}

static void cmd_cd(CTX, int argc, char** argv)
{
	int ret;
	char* dir;

	if(argc == 1) {
		if(!(dir = getenv(ctx->envp, "HOME")))
			dir = "/";
	} else if(argc == 2) {
		dir = argv[1];
	} else {
		return warn("too many arguments", NULL, 0);
	}

	if((ret = sys_chdir(dir)) < 0)
		return warn(NULL, dir, ret);

	prep_prompt(ctx);
}

static void cmd_set(CTX, int argc, char** argv)
{
	if(argc < 2)
		return envp_dump_all(ctx);
	if(argc == 2)
		return envp_dump(ctx, argv[1]);
	if(argc == 3)
		return envp_set(ctx, argv[1], argv[2]);

	warn("too many arguments", NULL, 0);
}

static void cmd_unset(CTX, int argc, char** argv)
{
	int i;

	if(argc < 2)
		return warn("too few arguments", NULL, 0);

	for(i = 1; i < argc; i++)
		envp_unset(ctx, argv[i]);
}

static void cmd_dot(CTX, int argc, char** argv)
{
	if(argc > 1)
		return warn("too many arguments", NULL, 0);

	list_cwd(ctx);

	prep_prompt(ctx);
}

static void cmd_ddot(CTX, int argc, char** argv)
{
	int ret;

	if(argc > 1)
		return warn("too many arguments", NULL, 0);
	if((ret = sys_chdir("..")) < 0)
		return warn(NULL, "..", ret);

	prep_prompt(ctx);
}

static void cmd_exit(CTX, int argc, char** argv)
{
	if(argc > 1)
		return warn("too many arguments", NULL, 0);

	_exit(0x00);
}

static const struct builtin {
	char name[8];
	void (*call)(CTX, int argc, char** argv);
} builtins[] = {
	{ ".",     cmd_dot   },
	{ "..",    cmd_ddot  },
	{ "cd",    cmd_cd    },
	{ "set",   cmd_set   },
	{ "unset", cmd_unset },
	{ "echo",  cmd_echo  },
	{ "exit",  cmd_exit  },
};

static const struct builtin* find_builtin(char* name)
{
	const struct builtin* bi;

	for(bi = builtins; bi < ARRAY_END(builtins); bi++)
		if(!strncmp(name, bi->name, sizeof(bi->name)))
			return bi;

	return NULL;
}

static void runbi(CTX, const struct builtin* bi, int argc, char** argv)
{
	bi->call(ctx, argc, argv);
}

static int trywaitpid(int pid, int* status)
{
	int ret;

	if((ret = sys_waitpid(-1, status, WNOHANG)) < 0) {
		if(ret != -EAGAIN)
			warn("wait", NULL, ret);
		return ret;
	}

	if(ret != pid) /* stray child */
		return -ESRCH;

	return ret;
}

static int child(char* exe, char** argv, char** envp)
{
	sigset_t mask;

	sigemptyset(&mask);
	sys_sigprocmask(SIG_SETMASK, &mask, NULL);
	sys_prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);

	int ret = execvpe(exe, argv, envp);

	fail(NULL, exe, ret);
}

static void spawn(CTX, char* exe, char** argv)
{
	int pid, status;
	int rd, fd = ctx->sigfd;
	struct sigevent se;

	if((pid = sys_fork()) < 0)
		return warn("fork", NULL, pid);
	if(pid == 0)
		_exit(child(exe, argv, ctx->envp));

	while((rd = sys_read(fd, &se, sizeof(se))) > 0) {
		if(rd < (int)sizeof(se))
			quit(ctx, "bad sigevent size", NULL, rd);

		if(se.signo == SIGINT)
			sys_kill(pid, SIGINT);
		else if(se.signo == SIGQUIT)
			sys_kill(pid, SIGQUIT);
		else if(se.signo != SIGCHLD)
			continue;
		else if(trywaitpid(pid, &status) >= 0)
			break;
	}

	if(WIFSIGNALED(status))
		warn("killed by signal", NULL, WTERMSIG(status));
}

/* Interactive shell gets to deal with mistyped commands a lot,
   and should handle them well. The code below is effectively execvpe()
   but it does access() check before trying to fork. This way there's no
   extra fork() if there's nothing to exec, and no confusion between exec
   failures and abnormal exit of the command being run. */

static int try_cmd_at(CTX, char** argv, char* ds, char* de)
{
	char* cmd = *argv;
	int clen = strlen(cmd);
	long dlen = de - ds;

	FMTBUF(p, e, path, clen + dlen + 2);
	p = fmtraw(p, e, ds, dlen);
	p = fmtchar(p, e, '/');
	p = fmtraw(p, e, cmd, clen);
	FMTEND(p, e);

	if(sys_access(path, X_OK))
		return 0;

	spawn(ctx, path, argv);

	return 1;
}

static void pathwalk(CTX, char** argv)
{
	char* path;

	if(!(path = getenv(ctx->envp, "PATH")))
		return warn("no $PATH set", NULL, 0);

	char* p = path;
	char* e = path + strlen(path);

	while(p < e) {
		char* q = strecbrk(p, e, ':');

		if(try_cmd_at(ctx, argv, p, q))
			return;

		p = q + 1;
	}

	warn("command not found:", *argv, 0);
}

static int lookslikepath(const char* file)
{
	const char* p;

	for(p = file; *p; p++)
		if(*p == '/')
			return 1;

	return 0;
}

void execute(CTX, int argc, char** argv)
{
	const struct builtin* bi;

	if(lookslikepath(*argv))
		return spawn(ctx, *argv, argv);
	if((bi = find_builtin(*argv)))
		return runbi(ctx, bi, argc, argv);

	return pathwalk(ctx, argv);
}
