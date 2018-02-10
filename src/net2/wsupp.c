#include <bits/socket/packet.h>
#include <bits/socket/unix.h>
#include <bits/ioctl/socket.h>
#include <bits/arp.h>
#include <bits/ether.h>

#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/socket.h>

#include <errtag.h>
#include <endian.h>
#include <string.h>
#include <sigset.h>
#include <util.h>

#include "common.h"
#include "wsupp.h"

ERRTAG("wsupp");

char** environ;

static sigset_t defsigset;
static struct pollfd pfds[3+NCONNS];
static int npfds;
static struct timespec pollts;
static int timerset;

int opermode;
int pollset;
int sigterm;
int done;

static void sighandler(int sig)
{
	switch(sig) {
		case SIGINT:
		case SIGTERM: sigterm = 1;
	}
}

static void sigaction(int sig, struct sigaction* sa)
{
	int ret;

	if((ret = sys_sigaction(sig, sa, NULL)) < 0)
		fail("sigaction", NULL, ret);
}

static void setup_signals(void)
{
	SIGHANDLER(sa, sighandler, 0);

	sigaddset(&sa.mask, SIGINT);
	sigaddset(&sa.mask, SIGTERM);
	sigaddset(&sa.mask, SIGHUP);
	sigaddset(&sa.mask, SIGALRM);

	sigaction(SIGINT,  &sa);
	sigaction(SIGTERM, &sa);
	sigaction(SIGHUP,  &sa);
	sigaction(SIGALRM, &sa);

	sa.handler = SIG_IGN;

	sigaction(SIGPIPE, &sa);
}

static void set_pollfd(struct pollfd* pfd, int fd)
{
	if(fd > 0) {
		pfd->fd = fd;
		pfd->events = POLLIN;
	} else {
		pfd->fd = -1;
		pfd->events = 0;
	}
}

static void close_conn(struct conn* cn)
{
	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));
	pollset = 0;
}

static void check_conn(struct pollfd* pf, struct conn* cn)
{
	if(pf->revents & POLLIN)
		handle_conn(cn);
	if(pf->revents & ~POLLIN)
		close_conn(cn);
}

static void check_netlink(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		handle_netlink();
	if(pf->revents & ~POLLIN)
		quit("lost netlink connection", NULL, 0);
}

static void check_control(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		handle_control();
	if(pf->revents & ~POLLIN)
		quit("lost control socket", NULL, 0);

	pollset = 0;
}

static void check_rawsock(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		handle_rawsock();
	if(!(pf->revents & ~POLLIN))
		return;

	sys_close(rawsock);
	rawsock = -1;
	pf->fd = -1;
}

static void check_rfkill(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		handle_rfkill();
	if(!(pf->revents & ~POLLIN))
		return;

	sys_close(rfkill);
	rfkill = -1;
	pf->fd = -1;
}

static void update_pollfds(void)
{
	set_pollfd(&pfds[0], netlink);
	set_pollfd(&pfds[1], rawsock);
	set_pollfd(&pfds[2], ctrlfd);
	set_pollfd(&pfds[3], rfkill);

	int i, n = 4;

	for(i = 0; i < nconns; i++)
		set_pollfd(&pfds[n+i], conns[i].fd);

	npfds = n + nconns;
	pollset = 1;
}

static void check_polled_fds(void)
{
	int i, n = 4;

	for(i = 0; i < nconns; i++)
		check_conn(&pfds[n+i], &conns[i]);

	check_netlink(&pfds[0]);
	check_rawsock(&pfds[1]);
	check_control(&pfds[2]);
	check_rfkill(&pfds[3]);
}

void clr_timer(void)
{
	pollts.sec = 0;
	pollts.nsec = 0;
	timerset = 0;
}

void set_timer(int seconds)
{
	pollts.sec = seconds;
	pollts.nsec = 0;
	timerset = 1;
}

int get_timer(void)
{
	return timerset ? pollts.sec : -1;
}

static void timer_expired(void)
{
	clr_timer();

	if(authstate == AS_NETDOWN) {
		if(!rfkilled)
			opermode = OP_EXIT;
		else
			authstate = AS_IDLE;
		return;
	}

	if(authstate == AS_CONNECTED)
		routine_bg_scan();
	else if(authstate != AS_IDLE)
		abort_connection();
	else
		routine_fg_scan();
}

static void shutdown(void)
{
	sigterm = 0;

	switch(opermode) {
		case OP_EXIT:
		case OP_EXITREQ:
			quit("second exit request", NULL, 0);
	}
	switch(authstate) {
		case AS_IDLE:
		case AS_NETDOWN:
			opermode = OP_EXIT;
			return;
	}

	if(start_disconnect() < 0)
		opermode = OP_EXIT;
	else
		opermode = OP_EXITREQ;
}

int main(int argc, char** argv, char** envp)
{
	int i = 1, ret;
	char* name;

	if(i < argc)
		name = argv[i++];
	else
		fail("too few arguments", NULL, 0);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	environ = envp;

	setup_signals();
	setup_netlink();
	setup_iface(name);
	setup_control();
	retry_rfkill();

	opermode = OP_NEUTRAL;
	load_state();
	routine_fg_scan();

	while(opermode) {
		struct timespec* ts = timerset ? &pollts : NULL;

		if(!pollset)
			update_pollfds();
		if((ret = sys_ppoll(pfds, npfds, ts, &defsigset)) > 0)
			check_polled_fds();
		else if(ret == 0)
			timer_expired();
		else if(ret != -EINTR)
			quit("ppoll", NULL, ret);
		if(sigterm)
			shutdown();

		save_config();
	}

	save_state();
	unlink_control();

	return 0;
}
