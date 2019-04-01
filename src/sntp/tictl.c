#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <main.h>
#include <util.h>

#include "common.h"

ERRTAG("tictl");

struct top {
	int argc;
	int argi;
	char** argv;

	int fd;
	struct ucbuf uc;
	struct urbuf ur;
	char txbuf[64];
	char rxbuf[512];
};

#define CTX struct top* ctx __attribute__((unused))
#define UC (&ctx->uc)

static void prep_context(CTX, int argc, char** argv)
{
	int i = 1;

	if(i < argc && argv[i][0] == '-' && argv[i++][1])
		fail("no options allowed", NULL, 0);

	ctx->argc = argc;
	ctx->argv = argv;
	ctx->argi = i;

	ctx->uc.brk = ctx->txbuf;
	ctx->uc.ptr = ctx->txbuf;
	ctx->uc.end = ctx->txbuf + sizeof(ctx->txbuf);

	ctx->ur.buf = ctx->rxbuf;
	ctx->ur.mptr = ctx->rxbuf;
	ctx->ur.rptr = ctx->rxbuf;
	ctx->ur.end = ctx->rxbuf + sizeof(ctx->rxbuf);
}

static int init_socket(CTX)
{
	int fd, ret;

	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = TIMED_CTRL
	};

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", "AF_UNIX", fd);

	if((ret = sys_connect(fd, &addr, sizeof(addr))) < 0)
		fail(NULL, addr.path, ret);

	ctx->fd = fd;

	return fd;
}

static void send_command(CTX)
{
	int wr, fd;
	char* txbuf = ctx->uc.brk;
	int txlen = ctx->uc.ptr - ctx->uc.brk;

	if(!(fd = ctx->fd))
		fd = init_socket(ctx);

	if((wr = sys_send(fd, txbuf, txlen, 0)) < 0)
		fail("send", NULL, wr);
	if(wr != txlen)
		fail("incomplete send", NULL, 0);
}

static struct ucmsg* recv_reply(CTX)
{
	struct urbuf* ur = &ctx->ur;
	int ret, fd = ctx->fd;

	if((ret = uc_recv(fd, ur, 1)) < 0)
		fail("recv", NULL, ret);

	return ur->msg;
}

static void send_check(CTX)
{
	struct ucmsg* msg;

	send_command(ctx);

	msg = recv_reply(ctx);

	if(msg->cmd > 0)
		fail("unexpected notification", NULL, 0);
	if(msg->cmd < 0)
		fail(NULL, NULL, msg->cmd);
}

static struct ucmsg* send_recv(CTX)
{
	send_command(ctx);

	return recv_reply(ctx);
}

static char* fmt_state(char* p, char* e, struct ucmsg* msg)
{
	int* sp = uc_get_int(msg, ATTR_STATE);
	int state = sp ? *sp : 0;
	char* tag;

	switch(state) {
		case TS_IDLE:
			tag = "Stopped";
			break;
		case TS_SELECT:
		case TS_PING_SENT:
		case TS_PING_WAIT:
			tag = "Picking server";
			break;
		case TS_POLL_SENT:
		case TS_POLL_WAIT:
			tag = "Synchronized";
			break;
		default:
			return p;
	}

	return fmtstr(p, e, tag);
}

static char* fmt_server(char* p, char* e, struct ucmsg* msg)
{
	struct ucattr* at = uc_get(msg, ATTR_ADDR);
	int* port = uc_get_int(msg, ATTR_PORT);

	if(!at || !port)
		return p;

	p = fmtstr(p, e, ", server ");

	if(uc_paylen(at) == 4)
		p = fmtip(p, e, uc_payload(at));
	else if(uc_paylen(at) == 16)
		p = fmtstr(p, e, "(ipv6)");
	else
		p = fmtstr(p, e, "(---)");

	if(*port != 123) {
		p = fmtstr(p, e, ":");
		p = fmtint(p, e, *port);
	}

	//int* stratum = uc_get_int(msg, ATTR_STRATUM);

	//if(stratum) {
	//	p = fmtstr(p, e, " (stratum ");
	//	p = fmtint(p, e, *stratum);
	//	p = fmtstr(p, e, ")");
	//}

	return p;
}

static char* fmt_dt_int(char* p, char* e, int dt)
{
	int sec = dt % 60; dt /= 60;
	int min = dt % 60; dt /= 60;
	int hr = dt % 24;  dt /= 24;
	int day = dt;

	if(day) {
		p = fmtstr(p, e, " ");
		p = fmtint(p, e, day);
		p = fmtstr(p, e, "d");
	} if(hr) {
		p = fmtstr(p, e, " ");
		p = fmtint(p, e, hr);
		p = fmtstr(p, e, "h");
	} if(min && !day) {
		p = fmtstr(p, e, " ");
		p = fmtint(p, e, min);
		p = fmtstr(p, e, "m");
	} if(!day && !hr) {
		p = fmtstr(p, e, " ");
		p = fmtint(p, e, sec);
		p = fmtstr(p, e, "s");
	}

	return p;
}

static char* fmt_dt_s64(char* p, char* e, int64_t dt)
{
	return fmt_dt_int(p, e, (int)dt);
}

static char* fmt_ntp_rt(char* p, char* e, int64_t dt)
{
	uint64_t ad = dt < 0 ? -dt : dt;
	uint64_t fr = ad & 0xFFFFFFFF;
	uint nsec = (uint)((fr * 1000000000 / (1ULL<<32)));
	uint usec = nsec / 1000;
	uint msec = usec / 1000;
	uint sec = (uint)(ad >> 32);

	if(dt < 0)
		p = fmtchar(p, e, '-');

	if(sec > 60) {
		p = fmtint(p, e, sec);
		p = fmtstr(p, e, "s");
	} else if(sec) {
		p = fmtint(p, e, sec);
		p = fmtstr(p, e, ".");
		p = fmtint(p, e, msec/100);
		p = fmtstr(p, e, "s");
	} else if(msec) {
		p = fmtint(p, e, msec);
		if(msec < 10) {
			p = fmtstr(p, e, ".");
			p = fmtint(p, e, usec/100);
		}
		p = fmtstr(p, e, "ms");
	} else if(usec) {
		p = fmtint(p, e, usec);
		p = fmtstr(p, e, "us");
	} else {
		p = fmtint(p, e, nsec);
		p = fmtstr(p, e, "ns");
	}

	return p;
}

static char* fmt_ntp_dt(char* p, char* e, int64_t dt)
{
	if(dt > 0)
		p = fmtchar(p, e, '+');

	return fmt_ntp_rt(p, e, dt);
}

static char* fmt_wakeup(char* p, char* e, struct ucmsg* msg)
{
	int* tp = uc_get_int(msg, ATTR_TIMELEFT);

	if(!tp) return p;

	p = fmtchar(p, e, '\n');
	p = fmtstr(p, e, "Next check in");
	p = fmt_dt_int(p, e, *tp);

	return p;
}

static char* fmt_times(char* p, char* e, struct ucmsg* msg)
{
	uint64_t* polltime = uc_get_u64(msg, ATTR_POLLTIME);
	uint64_t* synctime = uc_get_u64(msg, ATTR_SYNCTIME);
	int64_t* dt = uc_get_i64(msg, ATTR_OFFSET);
	int64_t* rt = uc_get_i64(msg, ATTR_RTT);

	struct timespec now;
	int ret;

	if(!polltime && !synctime)
		return p;

	if((ret = sys_clock_gettime(CLOCK_BOOTTIME, &now)) < 0) {
		warn("clock_gettime", "CLOCK_BOOTTIME", ret);
		return p;
	}

	uint64_t ref = now.sec;

	p = fmtchar(p, e, '\n');

	if(synctime) {
		p = fmtstr(p, e, "Last adjustment");
		p = fmt_dt_s64(p, e, ref - *synctime);
		p = fmtstr(p, e, " ago");
		if(dt) {
			p = fmtstr(p, e, ", offset ");
			p = fmt_ntp_dt(p, e, *dt);
		} if(rt) {
			p = fmtstr(p, e, " rtt ");
			p = fmt_ntp_rt(p, e, *rt);
			p = fmtstr(p, e, "");
		}
	} if(synctime && polltime) {
		p = fmtchar(p, e, '\n');
	} if(polltime) {
		p = fmtstr(p, e, "Last check");
		p = fmt_dt_s64(p, e, ref - *polltime);
		p = fmtstr(p, e, " ago, no adjustments");
	}

	return p;
}

static void cmd_status(CTX)
{
	struct ucmsg* msg;

	uc_put_hdr(UC, CMD_TI_STATUS);
	uc_put_end(UC);

	msg = send_recv(ctx);

	FMTBUF(p, e, output, 512);
	p = fmt_state(p, e, msg);
	p = fmt_server(p, e, msg);
	p = fmt_times(p, e, msg);
	p = fmt_wakeup(p, e, msg);
	FMTENL(p, e);

	writeall(STDOUT, output, p - output);
}

static int parse_ip4_server(char* arg, byte ip[4], int* port)
{
	char* p;

	if(!(p = parseip(arg, ip)))
		return 0;

	if(!*p)
		*port = 123;
	else if(*p != ':')
		fail("invalid address", arg, 0);
	else if(!(p = parseint(p+1, port)) || *p)
		fail("invalid port", arg, 0);

	return 1;
}

static void cmd_server(CTX)
{
	int i, n = ctx->argc - ctx->argi;

	uc_put_hdr(UC, CMD_TI_SERVER);

	if(n > 4)
		fail("too many servers", NULL, 0);

	for(i = 0; i < n; i++) {
		char* arg = ctx->argv[ctx->argi++];
		byte ip[16];
		int port;

		struct ucattr* at = uc_put_nest(UC, ATTR_SERVER);

		if(parse_ip4_server(arg, ip, &port))
			uc_put_bin(UC, ATTR_ADDR, ip, 4);
		else
			fail("invalid address", arg, 0);

		uc_put_int(UC, ATTR_PORT, port);

		uc_end_nest(UC, at);
	}

	uc_put_end(UC);

	send_check(ctx);
}

static void no_more_arguments(CTX)
{
	if((ctx->argc - ctx->argi) > 0)
		fail("too many arguments", NULL, 0);
}

static void cmd_retry(CTX)
{
	no_more_arguments(ctx);

	uc_put_hdr(UC, CMD_TI_RETRY);
	uc_put_end(UC);

	send_check(ctx);
}

static void cmd_reset(CTX)
{
	no_more_arguments(ctx);

	uc_put_hdr(UC, CMD_TI_RESET);
	uc_put_end(UC);

	send_check(ctx);
}

static const struct cmd {
	char name[8];
	void (*call)(CTX);
} commands[] = {
	{ "status", cmd_status },
	{ "server", cmd_server },
	{ "retry",  cmd_retry  },
	{ "reset",  cmd_reset  },
	{ "stop",   cmd_reset  }
};

static const struct cmd* find_command(CTX)
{
	const struct cmd* cc;

	if(ctx->argi >= ctx->argc)
		return &commands[0];

	char* name = ctx->argv[ctx->argi++];

	for(cc = commands; cc < ARRAY_END(commands); cc++)
		if(!strncmp(cc->name, name, sizeof(cc->name)))
			return cc;

	fail("unknown command", name, 0);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	const struct cmd* cc;

	memzero(ctx, sizeof(*ctx));
	prep_context(ctx, argc, argv);

	cc = find_command(ctx);

	cc->call(ctx);

	return 0;
}
