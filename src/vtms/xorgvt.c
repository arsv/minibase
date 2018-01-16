#include <bits/major.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/proc.h>
#include <sys/creds.h>
#include <sys/fprop.h>
#include <sys/signal.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <sigset.h>
#include <util.h>

/* Unlike Weston, the X server does not start any clients by itself.
   xinit does that instead, it starts the server, waits until it's
   ready to accept connections, and proceeds to start the top-level
   client, typically window manager or some sort of DE-start tool.

   This is a special version of xinit meant for a single use case:
   running Xorg server on a vtmux-managed console. It is not supposed
   to handle any other servers in any other circumstances (Xnest etc),
   and should not be in PATH.

   Running under vtmux means fd 3 is the control pipe. It must be
   passed to the server, but *not* to the top client. To match Weston
   config, the script that runs this should set WESTON_LAUNCHER_SOCK. */

ERRTAG("xorgvt");

#define OPTS "sav"
#define OPT_s (1<<0)
#define OPT_a (1<<1)
#define OPT_v (1<<2)

struct top {
	int opts;

	char** envp;

	char* server;
	char* client;
	char* display;

	char** sargs;
	int sargn;
	char** cargs;
	int cargn;

	int spid;
	int cpid;

	int ctlfd;
	int sigfd;

	sigset_t empty;
	sigset_t block;
};

#define CTX struct top* ctx

static void quit(CTX, const char* msg, char* arg, int err)
{
	fail(msg, arg, err);
}

static void sig_ignore(int sig)
{
	int ret;

	struct sigaction sa = {
		.handler = SIG_IGN,
		.flags = 0,
		.restorer = NULL
	};

	if((ret = sys_sigaction(sig, &sa, NULL)) < 0)
		warn("signal", NULL, ret);
}

static int recv_sig(CTX)
{
	int rd, fd = ctx->sigfd;
	struct sigevent se;

	if((rd = sys_read(fd, &se, sizeof(se))) < 0)
		quit(ctx, "read", "signalfd", rd);
	if(rd < (int)sizeof(se))
		quit(ctx, "bad sigevent size", NULL, rd);

	return se.signo;
}

static void inserver(CTX)
{
        sys_sigprocmask(SIG_SETMASK, &ctx->empty, NULL);

	sig_ignore(SIGTTIN);
	sig_ignore(SIGTTOU);
	sig_ignore(SIGUSR1);

        sys_setpgid(0, sys_getpid());
}

static void inclient(CTX)
{
	sys_close(ctx->ctlfd);
        sys_sigprocmask(SIG_SETMASK, &ctx->empty, NULL);

        sys_setpgid(0, sys_getpid());

}

static int spawn(CTX, char** argv, char** envp, void (*setup)(CTX))
{
	int pid;

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);
	if(pid == 0) {
		setup(ctx);
		int ret = execvpe(*argv, argv, envp);
		fail(NULL, *argv, ret);
	}

	return pid;
}

/* For the client, remove any WESTON_* variables from envp */

static int count_env(char** envp)
{
	char** p;

	for(p = envp; *p; p++)
		;

	return p - envp;
}

static void append(int argc, char** argv, int* argi, char** strs, int n)
{
	int i = *argi;

	if(i + n > argc)
		fail("out of ptr space", NULL, 0);

	memcpy(argv + i, strs, n*sizeof(char*));

	*argi = i + n;
}

static void addarg(int argc, char** argv, int* argi, char* str)
{
	int i = *argi;

	if(i >= argc)
		fail("out of ptr space", NULL, 0);

	argv[i] = str;

	*argi = i + 1;
}

static void endarg(int argc, char** argv, int* argi)
{
	addarg(argc, argv, argi, NULL);
}

static void filter(int envc, char** envp, int* envi, char** origenvp)
{
	char* pref = "WESTON_";
	int plen = strlen(pref);

	for(char** p = origenvp; *p; p++) {
		if(!strncmp(*p, pref, plen))
			continue;
		addarg(envc, envp, envi, *p);
	}
}

static void start_client(CTX)
{
	char** origenvp = ctx->envp;
	int origenvc = count_env(origenvp);

	FMTBUF(p, e, display, 30);
	p = fmtstr(p, e, "DISPLAY=");
	p = fmtstr(p, e, ctx->display);
	FMTEND(p, e);

	int argc = 1 + ctx->cargn + 1;
	int argi = 0;
	char* argv[argc];

	addarg(argc, argv, &argi, ctx->client);
	append(argc, argv, &argi, ctx->cargs, ctx->cargn);
	endarg(argc, argv, &argi);

	int envc = origenvc + 3;
	int envi = 0;
	char* envp[envc];

	filter(envc, envp, &envi, origenvp);
	addarg(envc, envp, &envi, display);
	endarg(envc, envp, &envi);

	ctx->cpid = spawn(ctx, argv, envp, inclient);
}

/* Server takes some time to initialize and xinit must wait until
   the socket becomes connectable. The server should send SIGUSR1
   once it's ready. */

static void start_server(CTX)
{
	char** envp = ctx->envp;

	int argc = 3 + ctx->sargn + 1;
	int argi = 0;
	char* argv[argc];

	addarg(argc, argv, &argi, ctx->server);
	addarg(argc, argv, &argi, ctx->display);

	if(ctx->opts & OPT_v) {
		addarg(argc, argv, &argi, "-verbose");
		addarg(argc, argv, &argi, "4");
	}

	append(argc, argv, &argi, ctx->sargs, ctx->sargn);
	endarg(argc, argv, &argi);

	ctx->spid = spawn(ctx, argv, envp, inserver);
}

static void wait_x_signal(CTX)
{
	int term = 0;
	int spid = ctx->spid;

	while(1) {
		int sig = recv_sig(ctx);

		if(sig == SIGUSR1 && !term)
			break;
		if(sig == SIGTERM || sig == SIGINT)
			sys_kill(term = spid, sig);
		if(sig != SIGCHLD)
			continue;

		int pid, status;

		if((pid = sys_waitpid(-1, &status, WNOHANG)) < 0)
			fail("waitpid", NULL, pid);
		if(pid == 0)
			continue;
		if(pid != spid)
			continue;

		ctx->spid = 0;

		if(term) _exit(-1);

		fail("X server failed to start", NULL, 0);
	}
}

/* During normal shutdown, the client exits spontaneously on user
   command and xinit terminates the server.

   Regardless of which process dies first, xinit should terminate
   the other one and waitpid() both of them. */

static int wait_sigchld(CTX)
{
	int spid = ctx->spid;
	int cpid = ctx->cpid;
	int term = 0, server = 0;
	int cstatus;

	while(spid && cpid) {
		int sig = recv_sig(ctx);

		if(sig == SIGTERM || sig == SIGINT)
			sys_kill(term = cpid, sig);
		if(sig != SIGCHLD)
			continue;

		int pid, status;

		if((pid = sys_waitpid(-1, &status, WNOHANG)) < 0)
			fail("waitpid", NULL, pid);
		if(pid == 0)
			continue;

		if(pid == spid) {
			spid = 0;

			if(!cpid)
				break;
			if(term)
				continue;

			warn("X server died unexpectedly", NULL, 0);
			server = 1;
			sys_kill(cpid, SIGTERM);
		}

		if(pid == cpid) {
			cpid = 0;
			cstatus = status;

			if(!spid)
				break;

			/* normal course, terminate the server */
			sys_kill(spid, SIGTERM);
		}
	}

	if(term || server)
		return -1;
	if(!WIFEXITED(cstatus))
		return -1;

	return WEXITSTATUS(cstatus);
}

/* Quick check to make sure both commands are available in PATH.
   No point in starting rather heavy X server only to have the client
   diying immediately after. */

static int try_cmd_at(char* cmd, char* ds, char* de)
{
	int clen = strlen(cmd);
	long dlen = de - ds;
	char* dir = ds;

	FMTBUF(p, e, path, clen + dlen + 2);
	p = fmtraw(p, e, dir, dlen);
	p = fmtchar(p, e, '/');
	p = fmtraw(p, e, cmd, clen);
	FMTEND(p, e);

	return sys_access(path, X_OK);
}

static int check_in_path(char* cmd, char** envp)
{
	char* path;

	if(!(path = getenv(envp, "PATH")))
		return -ENOENT;

	char* p = path;
	char* e = path + strlen(path);

	while(p < e) {
		char* q = strecbrk(p, e, ':');

		if(try_cmd_at(cmd, p, q) >= 0)
			return 0;

		p = q + 1;
	}

	return -ENOENT;
}

static int looks_like_path(const char* file)
{
	const char* p;

	for(p = file; *p; p++)
		if(*p == '/')
			return 1;

	return 0;
}

static void check_command(CTX, char* cmd)
{
	int ret;

	if(looks_like_path(cmd))
		ret = sys_access(cmd, X_OK);
	else
		ret = check_in_path(cmd, ctx->envp);

	if(ret < 0)
		fail(NULL, cmd, ret);
}

static int looks_like_display(char* s)
{
	int c;

	if(*s != ':')
		return 0;

	while((c = *(++s)))
		if(c < '0' || c > '9')
			return 0;

	return 1;
}

static char* shift(int argc, char** argv, int* argi)
{
	int i = *argi;

	if(i >= argc)
		fail("too few arguments", NULL, 0);
	if(!strcmp(argv[i], "--"))
		fail("missing argument before --", NULL, 0);

	*argi = i + 1;

	return argv[i];
}

static int count_to_sep(int argc, char** argv, int* start)
{
	int s = *start;
	int i = s;

	while(i < argc && strcmp(argv[i], "--"))
		i++;

	int n = i - s;

	if(i < argc) i++;

	*start = i;

	return n;
}

static void set_commands(CTX, int argc, char** argv)
{
	int i = 1, opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->opts = opts;

	if(opts & OPT_a && (opts & (OPT_s | OPT_v)))
		fail("cannot use -a with -sv", NULL, 0);

	if(opts & OPT_a) {
		ctx->server = shift(argc, argv, &i);
		ctx->display = shift(argc, argv, &i);

		ctx->sargs = argv + i;
		ctx->sargn = count_to_sep(argc, argv, &i);

		ctx->client = shift(argc, argv, &i);
		ctx->cargs = argv + i;
		ctx->cargn = argc - i;
	} else {
		if(opts & OPT_s)
			ctx->server = shift(argc, argv, &i);
		else
			ctx->server = "Xorg";

		ctx->display = shift(argc, argv, &i);

		ctx->client = shift(argc, argv, &i);
		ctx->cargs = argv + i;
		ctx->cargn = count_to_sep(argc, argv, &i);

		ctx->sargs = argv + i;
		ctx->sargn = argc - i;
	}

	if(!looks_like_display(ctx->display))
		fail("invalid display spec", ctx->display, 0);
}

static void set_launcher_sock_fd(CTX)
{
	char** envp = ctx->envp;
	char* wls;

	int ret, fd = 3;
	struct stat st;

	if(getenv(envp, "DISPLAY"))
		fail("DISPLAY is set, aborting", NULL, 0);
	if(!(wls = getenv(envp, "WESTON_LAUNCHER_SOCK")))
		fail("WESTON_LAUNCHER_SOCK is not set", NULL, 0);
	if(strcmp(wls, "3"))
		fail("WESTON_LAUNCHER_SOCK is not 3", NULL, 0);

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("fd 3 is not available", NULL, 0);
	if(!S_ISSOCK(st.mode))
		fail("fd 3 is not a socket", NULL, 0);

	if((ret = sys_fstat(STDIN, &st)) < 0)
		fail("no stdin, aborting", NULL, 0);
	if(!S_ISCHR(st.mode) || major(st.rdev) != TTY_MAJOR)
		fail("stdin is not a TTY", NULL, 0);

	ctx->ctlfd = fd;
}

static void sigprocmask(int how, sigset_t* mask)
{
	int ret;

	if((ret = sys_sigprocmask(how, mask, NULL)) < 0)
		fail("sigprocmask", NULL, ret);
}

static void open_signalfd(CTX)
{
	int fd;
	sigset_t* ss = &ctx->block;

	sigaddset(ss, SIGINT);
	sigaddset(ss, SIGTERM);
	sigaddset(ss, SIGUSR1);
	sigaddset(ss, SIGCHLD);

	sigprocmask(SIG_BLOCK, ss);

	if((fd = sys_signalfd(-1, ss, SFD_CLOEXEC)) < 0)
		fail("signalfd", NULL, fd);

	ctx->sigfd = fd;
}

int main(int argc, char** argv, char** envp)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	ctx->envp = envp;

	set_commands(ctx, argc, argv);
	set_launcher_sock_fd(ctx);

	check_command(ctx, ctx->server);
	check_command(ctx, ctx->client);

	open_signalfd(ctx);
	start_server(ctx);
	wait_x_signal(ctx);
	start_client(ctx);

	return wait_sigchld(ctx);
}
