#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <sys/kill.h>
#include <sys/fsnod.h>
#include <sys/signal.h>

#include <sigset.h>
#include <fail.h>
#include <exit.h>

#include "config.h"
#include "common.h"
#include "suhub.h"

ERRTAG = "suhub";

char** environ;

int nprocs;
int pids[NPROC];

static sigset_t defsigset;
struct pollfd pfds[NPROC+1];
int sigterm;
int sigchld;

static void child_died(int cfd, int pid, int status)
{
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

	check_listening(&pfds[0]); /* may modify nprocs! */

	for(i = 1; i < np + 1; i++)
		check_client(&pfds[i], &pids[i-1]);
}

static void sighandler(int sig)
{
	switch(sig) {
		case SIGINT:
		case SIGTERM: sigterm = 1; break;
		case SIGCHLD: sigchld = 1; break;
	}
}

static void sigaction(int sig, struct sigaction* sa, char* tag)
{
	xchk(sys_sigaction(sig, sa, NULL), "sigaction", tag);
}

static void sigprocmask(int how, sigset_t* mask, sigset_t* out)
{
	xchk(sys_sigprocmask(how, mask, out), "sigiprocmask", "SIG_BLOCK");
}

static void setup_signals(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTORER,
		.restorer = sigreturn
	};
	sigset_t* mask = &sa.mask;

	sigemptyset(mask);
	sigaddset(mask, SIGCHLD);

	sigprocmask(SIG_BLOCK, mask, &defsigset);

	sigaddset(mask, SIGALRM);
	sigaddset(mask, SIGTERM);
	sigaddset(mask, SIGINT);

	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP,  &sa, NULL);
	sigaction(SIGALRM, &sa, NULL);

	sa.flags &= ~SA_RESTART;
	sigaction(SIGCHLD, &sa, NULL);
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
