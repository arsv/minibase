#include <bits/arp.h>
#include <bits/auxvec.h>
#include <bits/ioctl/socket.h>
#include <bits/socket.h>
#include <bits/socket/packet.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/random.h>
#include <sys/timer.h>

#include <string.h>
#include <endian.h>
#include <sigset.h>
#include <util.h>
#include <main.h>

#include "dhconf.h"

ERRTAG("dhconf");

void quit(CTX, char* msg, int err)
{
	if(ctx->opts & OPT_q)
		_exit(0xFF);

	fail(ctx->ifname, msg, err);
}

static void set_timer_sec(CTX, int sec)
{
	int ret, tid = ctx->timerid;
	struct itimerspec its = {
		.interval = { 0, 0 },
		.value = { sec, 0 }
	};

	if((ret = sys_timer_settime(tid, 0, &its, NULL)) < 0)
		quit(ctx, "timer_settimee", ret);
}

void set_state(CTX, int state)
{
	ctx->state = state;
	ctx->count = 0;

	if(!ctx->timeact) return;

	set_timer_sec(ctx, 0);

	ctx->timeact = 0;
}

void set_timer(CTX, int sec)
{
	set_timer_sec(ctx, sec);

	ctx->timeact = 1;

	if(sec < 10) return;

	close_raw_socket(ctx);
}

static void create_timer(CTX)
{
	int ret, tid;
	struct sigevent sev = {
		.notify = SIGEV_SIGNAL,
		.signo = SIGALRM
	};

	if((ret = sys_timer_create(CLOCK_BOOTTIME, &sev, &tid)) < 0)
		fail("timer_create", NULL, 0);

	ctx->timerid = tid;
}

/* Try to come up with a somewhat random xid by pulling auxvec random
   bytes. Failure is not a big issue here, in the sense that DHCP is
   quite insecure by design and a truly random xid hardly improves that. */

static struct auxvec* find_auxvec_entry(CTX, int key)
{
	char** p = ctx->environ;

	while(*p) p++;

	struct auxvec* a = (struct auxvec*)(p + 1);

	for(; a->key; a++)
		if(a->key == key)
			return a;

	return NULL;
}

static void pick_random_xid(CTX)
{
	struct auxvec* rand;
	int ret;

	if((rand = find_auxvec_entry(ctx, AT_RANDOM))) {
		ctx->xid = (uint)rand->val;
	} else if((ret = sys_getrandom(&ctx->xid, sizeof(ctx->xid), 0)) < 0) {
		quit(ctx, "getrandom", ret);
	}
};

static void sighandler(int sig)
{
	(void)sig;
}

static void setup_signals(CTX)
{
	int fd, ret;
	struct sigset mask;
	int flags = SFD_NONBLOCK | SFD_CLOEXEC;

	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGCHLD);
	sigaddset(&mask, SIGALRM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGHUP);

	if((fd = sys_signalfd(-1, &mask, flags)) < 0)
		quit(ctx, "signalfd", fd);
	if((ret = sys_sigprocmask(SIG_BLOCK, &mask, NULL)) < 0)
		quit(ctx, "sigprocmask", ret);

	SIGHANDLER(sa, sighandler, 0);

	if((ret = sys_sigaction(SIGALRM, &sa, NULL)) < 0)
		quit(ctx, "sigaction", ret);

	ctx->sigfd = fd;
}

static void bind_raw_socket(CTX, int fd)
{
	struct sockaddr_ll addr = {
		.family = AF_PACKET,
		.ifindex = ctx->ifindex,
		.hatype = ARPHRD_NETROM,
		.pkttype = PACKET_HOST,
		.protocol = htons(ETH_P_IP),
		.halen = 6,
		.addr = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } /* broadcast */
	};
	int ret;

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		quit(ctx, "bind", fd);

	ctx->rawfd = fd;
}

void close_raw_socket(CTX)
{
	int ret, fd = ctx->rawfd;

	if(fd < 0)
		return;

	if((ret = sys_close(fd)) < 0)
		quit(ctx, "close", ret);

	ctx->rawfd = -1;
}

int reopen_raw_socket(CTX)
{
	int fd = ctx->rawfd;

	if(fd >= 0)
		return fd;

	if((fd = sys_socket(PF_PACKET, SOCK_DGRAM, 8)) < 0)
		quit(ctx, "raw socket", fd);

	bind_raw_socket(ctx, fd);

	return fd;
}

static void resolve_device(CTX, char* device)
{
	struct ifreq ifreq;
	int fd, ret;
	int nlen = strlen(device);

	ctx->ifname = device;

	memzero(&ifreq, sizeof(ifreq));

	if(nlen > (int)sizeof(ifreq.name))
		quit(ctx, device, -ENAMETOOLONG);
	if(nlen == sizeof(ifreq.name))
		memcpy(ifreq.name, device, nlen);
	else
		memcpy(ifreq.name, device, nlen + 1);

	if((fd = sys_socket(PF_PACKET, SOCK_DGRAM, 8)) < 0)
		quit(ctx, "raw socket", fd);

	if((ret = sys_ioctl(fd, SIOCGIFINDEX, &ifreq)) < 0)
		quit(ctx, "ioctl SIOCGIFINDEX", ret);

	ctx->ifindex = ifreq.ival;

	if((ret = sys_ioctl(fd, SIOCGIFHWADDR, &ifreq)) < 0)
		quit(ctx, "ioctl SIOCGIFHWADDR", ret);
	if(ifreq.addr.family != ARPHRD_ETHER)
		quit(ctx, "unexpected hwaddr family", 0);

	memcpy(ctx->ourmac, ifreq.addr.data, 6);

	bind_raw_socket(ctx, fd);
}

static void handle_signal(CTX, int sig)
{
	switch(sig) {
		case SIGALRM:
			ctx->timeact = 0;
			timeout_waiting(ctx);
			return;
		case SIGCHLD:
			check_child(ctx);
			return;
		case SIGINT:
			release_address(ctx);
			/* fallthrough */
		case SIGHUP:
			deconf_iface(ctx);
			/* fallthrough */
		case SIGTERM:
			kill_wait_pid(ctx);
			_exit(0xFF);
	}
}

static void check_signals(CTX, int revents)
{
	struct siginfo si;
	int rd, fd = ctx->sigfd;

	if(revents & ~POLLIN)
		quit(ctx, "lost signal fd", 0);
	if(!(revents & POLLIN))
		return;

	if((rd = sys_read(fd, &si, sizeof(si))) < 0) {
		if(rd == -EAGAIN)
			return;
		quit(ctx, "signalfd read", rd);
	} else if(rd == 0) {
		quit(ctx, "signalfd EOF", 0);
	}

	handle_signal(ctx, si.signo);
}

static void check_incoming(CTX, int revents)
{
	if(revents & ~POLLIN)
		quit(ctx, "raw socket lost", 0);
	if(!(revents & POLLIN))
		return;

	recv_incoming(ctx);
}

static void poll(CTX)
{
	int ret;
	struct pollfd pfds[] = {
		{ .fd = ctx->sigfd, .events = POLLIN },
		{ .fd = ctx->rawfd, .events = POLLIN }
	};
	int npfds = ARRAY_SIZE(pfds);

	if((ret = sys_ppoll(pfds, npfds, NULL, NULL)) < 0)
		quit(ctx, "ppoll", ret);

	check_signals(ctx, pfds[0].revents);
	check_incoming(ctx, pfds[1].revents);
}

static void setup_args(CTX, int argc, char** argv)
{
	int i = 1;

	ctx->environ = argv + argc + 1;

	if(i < argc && argv[i][0] == '-')
		ctx->opts = argbits(OPTS, argv[i++] + 1);
	if(i >= argc)
		fail("too few arguments", NULL, 0);

	resolve_device(ctx, argv[i++]);

	if(i + 1 < argc)
		fail("too many arguments", NULL, 0);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	setup_args(ctx, argc, argv);
	pick_random_xid(ctx);
	setup_signals(ctx);
	create_timer(ctx);

	start_discover(ctx);

	while(1) poll(ctx);
}
