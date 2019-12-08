#include <bits/socket/unix.h>
#include <bits/ioctl/tty.h>
#include <bits/ioctl/pty.h>
#include <bits/errno.h>
#include <sys/signal.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/ppoll.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <printf.h>
#include <sigset.h>
#include <util.h>
#include <main.h>
#include <cmsg.h>
#include <netlink.h>
#include <output.h>

#include "common.h"

ERRTAG("nestvt");

struct top {
	int sfd;
	int cfd;
	int mfd;

	int xid;

	int cols;
	int rows;

	struct termios tio;
	struct winsize ws;
	int termactive;

	char buf[1024];
};

#define CTX struct top* ctx __unused
#define MSG struct ucmsg* msg __unused
#define UC struct ucbuf* uc

#define SMALL_REQUEST 1024

static void term_reset(CTX)
{
	int ret;

	if(!ctx->termactive)
		return;

	ctx->termactive = 0;

	if((ret = sys_ioctl(0, TCSETS, &ctx->tio)) < 0)
		fail("ioctl", "TCSETS", ret);
}

static void noreturn quit(CTX, const char* msg, char* arg, int ret)
{
	term_reset(ctx);

	fail(msg, arg, ret);
}

static void noreturn clean_exit(CTX, int code)
{
	term_reset(ctx);

	_exit(code);
}

static void init_local_tty(CTX)
{
	int ret, fd = STDIN;

	if((ret = sys_ioctl(fd, TIOCGWINSZ, &ctx->ws)) < 0)
		fail("ioctl", "TIOCGWINSZ", ret);
	if(ctx->ws.row <= 0 || ctx->ws.col <= 0)
		fail("terminal does not report window size", NULL, 0);
}

static void set_initial_size(CTX)
{
	int ret, fd = ctx->mfd;

	if((ret = sys_ioctl(fd, TIOCSWINSZ, &ctx->ws)) < 0)
		quit(ctx, "ioctl", "TIOCSWINSZ", ret);
}

static void update_term_size(CTX)
{
	int mfd = ctx->mfd;
	int ifd = STDIN;
	int ret;

	if(mfd < 0) return;

	if((ret = sys_ioctl(ifd, TIOCGWINSZ, &ctx->ws)) < 0)
		quit(ctx, "ioctl", "TIOCGWINSZ", ret);
	if((ret = sys_ioctl(mfd, TIOCSWINSZ, &ctx->ws)) < 0)
		quit(ctx, "ioctl", "TIOCSWINSZ", ret);
}

static void term_clear(CTX)
{
	int ret;
	struct termios ts;

	if((ret = sys_ioctl(0, TCGETS, &ts)) < 0)
		fail("ioctl", "TCGETS", ret);

	memcpy(&ctx->tio, &ts, sizeof(ts));
	ts.iflag |= IUTF8;
	ts.iflag &= ~(IGNPAR | ICRNL | IXON | IMAXBEL);
	ts.oflag &= ~(OPOST | ONLCR);
	ts.lflag &= ~(ICANON | ECHO | ISIG);

	if((ret = sys_ioctl(0, TCSETS, &ts)) < 0)
		fail("ioctl", "TCSETS", ret);

	ctx->termactive = 1;
}

static void parse_ancillary(CTX, struct ucaux* ux)
{
	int fd;

	if((fd = ux_getf1(ux)) < 0)
		fail("ancillary", NULL, fd);

	ctx->mfd = fd;
}

static void parse_proc_attr(CTX, struct ucattr* msg)
{
	int* p;

	if(!(p = uc_get_int(msg, ATTR_XID)))
		fail("no xid in reply", NULL, 0);

	ctx->xid = *p;
}

static void recv_mfd_reply(CTX)
{
	int ret, cmd, fd = ctx->cfd;
	char buf[64];
	struct ucattr* msg;
	struct ucaux ux;

	if((ret = uc_recv_aux(fd, buf, sizeof(buf), &ux)) < 0)
		fail("recv", NULL, ret);
	if(!(msg = uc_msg(buf, ret)))
		fail("recv:", "invalid message", 0);

	if((cmd = uc_repcode(msg)) < 0)
		fail(NULL, NULL, cmd);
	else if(cmd > 0)
		fail("unexpected notification", NULL, 0);

	parse_ancillary(ctx, &ux);

	parse_proc_attr(ctx, msg);
}

static int connect_socket(CTX)
{
	int ret, fd;
	char* path = CONTROL;

	if((fd = sys_socket(AF_UNIX, SOCK_SEQPACKET, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = uc_connect(fd, path)) < 0)
		fail(NULL, path, ret);

	ctx->cfd = fd;

	return fd;
}

static void send_request(CTX, struct ucbuf* uc)
{
	int ret, fd = connect_socket(ctx);

	if((ret = uc_send(fd, uc)) < 0)
		fail("send", NULL, ret);
}

static int estimate_request_size(int argn, char** args, char** envp)
{
	int i, sum = 0, count = 0;
	char* evar;

	for(i = 0; i < argn; i++) {
		char* arg = args[i];
		int len = strlen(arg);
		len = (len + 3) & ~3;
		sum += len;
		count++;
	}

	while((evar = *envp++)) {
		int len = strlen(evar);
		len = (len + 3) & ~3;
		sum += len;
		count++;
	}

	return 64 + 4*count + sum;
}

static void spawn_request(CTX, UC, int argc, char** argv, char** envp)
{
	char *var;
	struct ucattr* at;

	uc_put_hdr(uc, CMD_START);

	at = uc_put_strs(uc, ATTR_ARGV);
	for(int i = 0; i < argc; i++)
		uc_add_str(uc, argv[i]);
	uc_end_strs(uc, at);

	at = uc_put_strs(uc, ATTR_ENVP);
	while((var = *envp++))
		uc_add_str(uc, var);
	uc_end_strs(uc, at);

	send_request(ctx, uc);
}

static void spawn(CTX, int argn, char** args)
{
	char** envp = args + argn + 1;
	int est = estimate_request_size(argn, args, envp);
	struct ucbuf uc;

	if(est <= SMALL_REQUEST) {
		char buf[SMALL_REQUEST+20];

		uc_buf_set(&uc, buf, sizeof(buf));

		spawn_request(ctx, &uc, argn, args, envp);
	} else if(est >= 2*PAGE) {
		fail("request too large", NULL, 0);
	} else {
		char* buf = sys_brk(NULL);
		char* end = sys_brk(buf + pagealign(est));
		long len = end - buf;

		uc_buf_set(&uc, buf, len);

		spawn_request(ctx, &uc, argn, args, envp);
	}

	recv_mfd_reply(ctx);
}

static int parse_xid(char* xidstr)
{
	int xid;
	char* p;

	if(!(p = parseint(xidstr, &xid)) || *p)
		fail("invalid xid:", xidstr, 0);

	return xid;
}

static void attach(CTX, char* xidstr)
{
	int xid = parse_xid(xidstr);
	char buf[128];
	struct ucbuf uc;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, CMD_ATTACH);
	uc_put_int(&uc, ATTR_XID, xid);

	send_request(ctx, &uc);

	recv_mfd_reply(ctx);
}

static void fetch_master(CTX, int argc, char** argv)
{
	if(argc < 2)
		fail("too few arguments", NULL, 0);

	if(argv[1][0] != '+')
		spawn(ctx, argc - 1, argv + 1);
	else if(argc > 2)
		fail("too many arguments", NULL, 0);
	else
		attach(ctx, argv[1] + 1);

	set_initial_size(ctx);
}

static void init_signal_fd(CTX)
{
	int fd, ret;
	struct sigset mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGWINCH);

	if((fd = sys_signalfd(-1, &mask, SFD_NONBLOCK)) < 0)
		fail("signalfd", NULL, 0);
	if((ret = sys_sigprocmask(SIG_SETMASK, &mask, NULL)) < 0)
		fail("sigprocmask", NULL, 0);

	ctx->sfd = fd;
}

static void check_termin(CTX, int events)
{
	if(events & ~POLLIN)
		quit(ctx, "lost", "stdin", 0);
	if(!(events & POLLIN))
		return;

	int ret, fd = STDIN;
	int out = ctx->mfd;
	char buf[512];

	if((ret = sys_read(fd, buf, sizeof(buf))) < 0)
		quit(ctx, "read", "stdin", ret);
	if((ret = writeall(out, buf, ret)) < 0)
		quit(ctx, "write", "master", ret);
}

static void check_master(CTX, int events)
{
	int ret, fd = ctx->mfd;
	char buf[512];

	if(events & ~POLLIN)
		ctx->mfd = -1;
	if(!(events & POLLIN))
		return;

	if((ret = sys_read(fd, buf, sizeof(buf))) < 0)
		quit(ctx, "read", "master", ret);
	if((ret = writeall(STDOUT, buf, ret)) < 0)
		quit(ctx, "write", "stdout", ret);
}

static void check_signal(CTX, int events)
{
	if(events & ~POLLIN)
		quit(ctx, "lost", "signalfd", 0);
	if(!(events & POLLIN))
		return;

	struct siginfo si;
	int ret, fd = ctx->sfd;

	if((ret = sys_read(fd, &si, sizeof(si))) < 0)
		quit(ctx, "read", "signalfd", ret);

	int sig = si.signo;

	if(sig == SIGWINCH)
		update_term_size(ctx);
	if(sig == SIGTERM)
		clean_exit(ctx, 0x00);
}

static void handle_notification(CTX, struct ucattr* msg)
{
	int cmd = uc_repcode(msg);

	if(cmd != REP_EXIT)
		fail("unexpected notification", NULL, cmd);

	int* exit = uc_get_int(msg, ATTR_EXIT);
	int status = exit ? *exit : 0;

	clean_exit(ctx, status ? 0xFF : 0x00);
}

static void check_control(CTX, int events)
{
	if(events & ~POLLIN)
		quit(ctx, "lost", "control", 0);
	if(!(events & POLLIN))
		return;

	int ret, fd = ctx->cfd;
	struct ucattr* msg;
	char buf[128];

	if((ret = uc_recv(fd, buf, sizeof(buf))) < 0)
		quit(ctx, "recv", "control", ret);
	if(!(msg = uc_msg(buf, ret)))
		quit(ctx, "recv", "invalid message", 0);

	handle_notification(ctx, msg);
}

static void noreturn main_loop(CTX)
{
	int ret;
	struct pollfd pfds[] = {
		{ .fd = STDIN,    .events = POLLIN },
		{ .fd = ctx->mfd, .events = POLLIN },
		{ .fd = ctx->sfd, .events = POLLIN },
		{ .fd = ctx->cfd, .events = POLLIN }
	};
again:
	if((ret = sys_ppoll(pfds, ARRAY_SIZE(pfds), NULL, NULL)) < 0)
		quit(ctx, "ppoll", NULL, ret);

	check_termin(ctx, pfds[0].revents);
	check_master(ctx, pfds[1].revents);
	check_signal(ctx, pfds[2].revents);
	check_control(ctx, pfds[3].revents);

	goto again;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	init_local_tty(ctx);
	init_signal_fd(ctx);

	fetch_master(ctx, argc, argv);

	term_clear(ctx);

	main_loop(ctx);
}
