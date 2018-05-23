#include <bits/socket/unix.h>

#include <sys/fpath.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/sched.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

#define CONTROL NLCDIR "/tserv"

ERRTAG("tserv");

static void sighandler(int sig)
{
	(void)sig;
	/* do nothing */
}

static void setup_signals(void)
{
	SIGHANDLER(sa, sighandler, 0);

	sys_sigaction(SIGINT, &sa, NULL);
	sys_sigaction(SIGTERM, &sa, NULL);
}

static void setup_socket(char* name)
{
	int fd, ret;
	struct sockaddr_un addr;
	uint nlen = strlen(name);

	addr.family = AF_UNIX;
	
	if(nlen > sizeof(addr.path) - 1)
		fail("name too long:", name, 0);

	memcpy(addr.path, name, nlen);
	addr.path[nlen] = '\0';

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", NULL, fd);
	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", addr.path, ret);
	if((ret = sys_listen(fd, 1)) < 0)
		fail("listen", NULL, ret);
}

int main(int argc, char** argv)
{
	if(argc > 2)
		fail("too many arguments", NULL, 0);

	char* name = argc > 1 ? argv[1] : "tserv";
	char* dir = RUN_CTRL;

	FMTBUF(p, e, path, strlen(name) + strlen(dir) + 10);
	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	setup_signals();
	setup_socket(path);

	sys_pause();

	sys_unlink(path);

	return 0;
}
