#include <bits/major.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/proc.h>
#include <sys/creds.h>
#include <sys/fprop.h>
#include <sys/signal.h>

#include <format.h>
#include <string.h>
#include <printf.h>
#include <sigset.h>
#include <util.h>
#include <main.h>

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

struct top {
	int opts;

	char** envp;

	char display[16];
	char server[64];
	char client[64];

	int spid;
	int cpid;
	int vtno;

	int ctlfd;
	int sigfd;

	sigset_t empty;
	sigset_t block;
};

#define CTX struct top* ctx

static void maybe_stop(int pid)
{
	int ret, status;

	if(pid <= 0)
		return;

	sys_kill(pid, SIGTERM);

	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		warn("waitpid", NULL, ret);
}

static void quit(CTX, const char* msg, char* arg, int err)
{
	maybe_stop(ctx->spid);
	maybe_stop(ctx->cpid);
	fail(msg, arg, err);
}

static void sig_ignore(int sig)
{
	SIGHANDLER(sa, SIG_IGN, 0);
	int ret;

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

static int spawn(CTX, char* path, char** envp, void (*setup)(CTX))
{
	int pid;

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);
	if(pid == 0) {
		char* argv[] = { path, NULL };
		setup(ctx);
		int ret = execvpe(path, argv, envp);
		fail(NULL, path, ret);
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

	/* gcc manages to derive this to be always false
	   because argc = n + constant */
	//if(i + n > argc)
	//	fail("out of ptr space", NULL, 0);
	(void)argc;

	memcpy(argv + i, strs, n*sizeof(char*));

	*argi = i + n;
}

static void addarg(int argc, char** argv, int* argi, char* str)
{
	int i = *argi;

	//if(i >= argc)
	//	fail("out of ptr space", NULL, 0);
	(void)argc;

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

	int envc = origenvc + 3;
	int envi = 0;
	char* envp[envc];

	filter(envc, envp, &envi, origenvp);
	addarg(envc, envp, &envi, ctx->display);
	endarg(envc, envp, &envi);

	ctx->cpid = spawn(ctx, ctx->client, envp, inclient);
}

/* Server takes some time to initialize and xinit must wait until
   the socket becomes connectable. The server should send SIGUSR1
   once it's ready. */

static void start_server(CTX)
{
	char** origenvp = ctx->envp;
	int origenvc = count_env(origenvp);

	int envc = origenvc + 3;
	int envi = 0;
	char* envp[envc];

	append(envc, envp, &envi, origenvp, origenvc);
	addarg(envc, envp, &envi, ctx->display);
	endarg(envc, envp, &envi);

	ctx->spid = spawn(ctx, ctx->server, envp, inserver);
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
	int cstatus = 0;

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

/* Quick check for required files to avoid forking and spawning
   the server just to find out that client's missing. */

static void check_command(char* path)
{
	int ret;

	if((ret = sys_access(path, X_OK)) < 0)
		fail(NULL, path, ret);
}

static int looks_like_basename(char* file)
{
	char* p;

	if(!*file)
		return 0;

	for(p = file; *p; p++)
		if(*p == '/')
			return 0;

	return 1;
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

static void set_default_display(CTX)
{
	char* p = ctx->display;
	char* e = p + sizeof(ctx->display) - 1;

	p = fmtstr(p, e, "DISPLAY=");
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, ctx->vtno);

	*p = '\0';
}

static void set_display(CTX, char* str)
{
	if(!looks_like_display(str))
		fail("invalid display spec:", str, 0);

	char* p = ctx->display;
	char* e = p + sizeof(ctx->display);

	p = fmtstr(p, e, "DISPLAY=");
	p = fmtstr(p, e, str);

	if(p >= e)
		fail("display name too long", NULL, 0);

	*p = '\0';
}

static void set_command(char* buf, uint len, const char* pref, char* val)
{
	if(val && !looks_like_basename(val))
		fail("must be basename:", val, 0);

	char* p = buf;
	char* e = p + len;

	p = fmtstr(p, e, "/etc/X11/");
	p = fmtstr(p, e, pref);
	if(val) p = fmtstr(p, e, val);

	if(p < e)
		;
	else if(val)
		fail("value too long:", val, 0);
	else
		fail("out of buffer space", NULL, 0);

	*p = '\0';
}

#define SET(field, pref, val) \
	set_command(field, sizeof(field), pref, val)

static void set_commands(CTX, int argc, char** argv)
{
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		fail("no options allowed", NULL, 0);

	if(i < argc && argv[i][0] == '+')
		SET(ctx->server, "server-", argv[i++] + 1);
	else
		SET(ctx->server, "server", NULL);

	if(i < argc && argv[i][0] == ':')
		set_display(ctx, argv[i++]);
	else
		set_default_display(ctx);

	if(i < argc)
		SET(ctx->client, "start-", argv[i++]);
	else
		SET(ctx->client, "client", NULL);

	if(i < argc)
		fail("too many arguments", NULL, 0);
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

	ctx->vtno = minor(st.rdev);
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

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	ctx->envp = argv + argc + 1;

	set_launcher_sock_fd(ctx);
	set_commands(ctx, argc, argv);

	check_command(ctx->server);
	check_command(ctx->client);

	open_signalfd(ctx);
	start_server(ctx);
	wait_x_signal(ctx);
	start_client(ctx);

	return wait_sigchld(ctx);
}
