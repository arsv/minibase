#include <bits/socket/unix.h>

#include <sys/socket.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/fpath.h>

#include <errtag.h>
#include <sigset.h>
#include <util.h>

#include "common.h"
#include "suhub.h"

ERRTAG("suhub");

char** environ;

int nprocs;
int pids[NPROC];

static sigset_t defsigset;
struct pollfd pfds[NPROC+1];
int sigterm;
int sigchld;

static void child_died(int cfd, int pid, int status)
{
	(void)pid; /* maybe check if it's the right one? */

	if(WIFSIGNALED(status))
		reply(cfd, REP_DEAD, ATTR_SIGNAL, WTERMSIG(status));
	else
		reply(cfd, REP_DEAD, ATTR_STATUS, WEXITSTATUS(status));
}

static void wait_pids(int flags)
{
	int pid, status;

	while((pid = sys_waitpid(-1, &status, flags)) > 0) {
		for(int i = 0; i < nprocs; i++) {
			if(pids[i] != pid)
				continue;

			struct pollfd* pf = &pfds[i+1];

			child_died(pf->fd, pid, status);

			pids[i] = -1;

			break;
		}
	}
}

static void retract_nprocs(void)
{
	while(nprocs > 0) {
		int i = nprocs - 1;

		if(pids[i] > 0)
			break;
		if(pfds[i+1].fd > 0)
			break;

		nprocs--;
	}
}

static void accept_connection(int sfd)
{
	int cfd;
	struct sockaddr_un addr;
	int addrlen = sizeof(addr);

	while((cfd = sys_accept(sfd, &addr, &addrlen)) >= 0) {
		if(nprocs < NPROC) {
			int i = nprocs++;
			pids[i] = 0;
			pfds[i+1].fd = cfd;
			pfds[i+1].events = POLLIN;
		} else {
			reply(cfd, -EAGAIN, 0, 0);
			sys_shutdown(cfd, SHUT_RDWR);
			sys_close(cfd);
		}
	}
}

static void check_listening(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		accept_connection(pf->fd);
	if(pf->revents & ~POLLIN)
		quit("control socket lost", NULL, 0);
}

static void check_client(struct pollfd* pf, int* cpid)
{
	int pid;

	if(pf->revents & POLLIN)
		handle(pf->fd, cpid);

	if(pf->revents & ~POLLIN) {
		sys_close(pf->fd);

		pf->fd = -1;

		if((pid = *cpid) > 0)
			sys_kill(pid, SIGHUP);
		else
			retract_nprocs();
	}
}

static void check_polled_fds(void)
{
	int i, np = nprocs;

	for(i = 1; i < np + 1; i++)
		check_client(&pfds[i], &pids[i-1]);

	check_listening(&pfds[0]);
}

static void sighandler(int sig)
{
	switch(sig) {
		case SIGINT:
		case SIGTERM: sigterm = 1; break;
		case SIGCHLD: sigchld = 1; break;
	}
}

static void setup_signals(void)
{
	SIGHANDLER(sa, sighandler, 0);
	int ret;

	sigaddset(&sa.mask, SIGCHLD);

	if((ret = sys_sigprocmask(SIG_BLOCK, &sa.mask, &defsigset)) < 0)
		fail("sigprocmask", NULL, ret);

	sigaddset(&sa.mask, SIGALRM);
	sigaddset(&sa.mask, SIGTERM);
	sigaddset(&sa.mask, SIGINT);

	if((ret = sys_sigaction(SIGINT,  &sa, NULL)) < 0)
		fail("sigaction", "SIGINT", ret);
	if((ret = sys_sigaction(SIGTERM, &sa, NULL)) < 0)
		fail("sigaction", "SIGTERM", ret);
	if((ret = sys_sigaction(SIGHUP,  &sa, NULL)) < 0)
		fail("sigaction", "SIGHUP", ret);
	if((ret = sys_sigaction(SIGALRM, &sa, NULL)) < 0)
		fail("sigaction", "SIGALRM", ret);

	sa.flags &= ~SA_RESTART;

	if((ret = sys_sigaction(SIGCHLD, &sa, NULL)) < 0)
		fail("sigaddset", "SIGCHLD", ret);
}

static void setup_ctrl(void)
{
	int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = CONTROL
	};
	int fd, ret;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", addr.path, ret);
	if((ret = sys_setsockopti(fd, SOL_SOCKET, SO_PASSCRED, 1)) < 0)
		fail("setsockopt", "SO_PASSCRED", ret);
	if((ret = sys_listen(fd, 1)))
		fail("listen", addr.path, ret);

	pfds[0].fd = fd;
	pfds[0].events = POLLIN;
}

static void kill_all_children(void)
{
	for(int i = 0; i < nprocs; i++)
		if(pids[i] > 0)
			sys_kill(pids[i], SIGTERM);
}

static int any_children_left(void)
{
	for(int i = 0; i < nprocs; i++)
		if(pids[i] > 0)
			return 1;
	return 0;
}

void quit(const char* msg, char* arg, int err)
{
	kill_all_children();

	while(any_children_left())
		wait_pids(0);

	sys_unlink(CONTROL);

	if(msg || arg || err)
		fail(msg, arg, err);
	else
		_exit(0);
}

int main(int argc, char** argv, char** envp)
{
	(void)argv;
	int ret;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	environ = envp;

	setup_ctrl();
	setup_signals();

	while(!sigterm) {
		ret = sys_ppoll(pfds, nprocs+1, NULL, &defsigset);

		if(sigchld)
			wait_pids(WNOHANG);
		if(ret == -EINTR)
			; /* signal has been caught and handled */
		else if(ret < 0)
			fail("ppoll", NULL, ret);
		else if(ret > 0)
			check_polled_fds();

		sigchld = 0;
	}

	sigterm = 0;

	quit(NULL, NULL, 0);
}
