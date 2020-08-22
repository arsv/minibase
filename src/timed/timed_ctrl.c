#include <bits/socket/inet.h>
#include <bits/socket/inet6.h>

#include <sys/socket.h>
#include <sys/ppoll.h>
#include <sys/timer.h>
#include <sys/file.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "timed.h"
#include "common.h"

#define UC struct ucbuf* uc
#define REPLIED 1

typedef struct ucattr* attr;

static int send_reply(CTX, struct conn* cn, struct ucbuf* uc)
{
	int ret, fd = cn->fd;

	if((ret = uc_send(fd, uc)) != -EAGAIN)
		return ret;
	if((ret = uc_wait_writable(fd)) < 0)
		return ret;

	if((ret = uc_send(fd, uc)) > 0)
		return ret;

	clear_client(ctx, cn);

	return REPLIED;
}

static int reply(CTX, struct conn* cn, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);

	return send_reply(ctx, cn, &uc);
}

static int time_left(CTX)
{
	int ret;
	struct itimerspec its;

	if(ctx->tistate != TI_ARMED)
		return ctx->alarm.sec;

	if((ret = sys_timer_gettime(ctx->timerid, &its)) < 0)
		return ret;

	return (int)its.value.sec;
}

static void put_server_addr(UC, struct serv* ss)
{
	if(ss->flags & SF_IPv6)
		uc_put_bin(uc, ATTR_ADDR, ss->addr, 16);
	else
		uc_put_bin(uc, ATTR_ADDR, ss->addr, 4);

	uc_put_int(uc, ATTR_PORT, ss->port);
}

static void put_rtt_in_us(UC, uint rtt)
{
	int64_t rtt64 = (int64_t)rtt;
	uint urtt = (uint)((rtt64 * 1000000) >> 32);
	uc_put_int(uc, ATTR_RTT, urtt);
}

static void report_ping(CTX, UC)
{
	int i, n = NSERVS;

	uc_put_hdr(uc, REP_PING);

	uc_put_int(uc, ATTR_TIMELEFT, time_left(ctx));

	for(i = 0; i < n; i++) {
		struct serv* sv = &ctx->servs[i];
		int flags = sv->flags;

		if(!(flags & SF_SET))
			continue;

		int tag = ATTR_SERVER;

		if(flags & (SF_FAIL | SF_KILL))
			tag = ATTR_FAILED;

		attr at = uc_put_nest(uc, tag);
		put_server_addr(uc, sv);
		if(flags & SF_RTT)
			put_rtt_in_us(uc, sv->rtt);
		uc_end_nest(uc, at);
	}
}

static void report_poll(CTX, UC)
{
	uc_put_hdr(uc, REP_POLL);

	uc_put_int(uc, ATTR_TIMELEFT, time_left(ctx));

	if(ctx->failures)
		uc_put_int(uc, ATTR_FAILURES, ctx->failures);

	if(ctx->synctime) {
		uc_put_i64(uc, ATTR_SYNCTIME, ctx->synctime);
		uc_put_i64(uc, ATTR_OFFSET, ctx->syncdt);
	} if(ctx->polltime != ctx->synctime) {
		uc_put_i64(uc, ATTR_POLLTIME, ctx->polltime);
	}

	if(ctx->current < 0)
		return;

	struct serv* ss = current(ctx);

	put_server_addr(uc, ss);

	if(ctx->polltime)
		put_rtt_in_us(uc, ctx->lastrtt);
}

static void report_idle(CTX, UC)
{
	uc_put_hdr(uc, REP_IDLE);
}

static void report_select(CTX, UC)
{
	uc_put_hdr(uc, REP_SELECT);
}

static int cmd_status(CTX, CN, MSG)
{
	char cbuf[512];
	struct ucbuf uc;
	int state = ctx->state;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));

	switch(state) {
		case TS_IDLE:
			report_idle(ctx, &uc);
			break;
		case TS_SELECT:
			report_select(ctx, &uc);
			break;
		case TS_PING_SENT:
		case TS_PING_WAIT:
			report_ping(ctx, &uc);
			break;
		case TS_POLL_SENT:
		case TS_POLL_WAIT:
			report_poll(ctx, &uc);
			break;
		default:
			return -EFAULT;
	}

	return send_reply(ctx, cn, &uc);
}

static struct serv* find_server_slot(CTX, void* ip, int alen, int port)
{
	int i, n = NSERVS;
	int flag;
	int mask = SF_IPv6;

	if(alen == 16)
		flag = SF_IPv6;
	else if(alen == 4)
		flag = 0;
	else return NULL;

	for(i = 0; i < n; i++) {
		struct serv* sv = &ctx->servs[i];

		if((sv->flags & mask) != flag)
			continue;
		if(sv->port != port)
			continue;
		if(memcmp(sv->addr, ip, alen))
			continue;

		return sv;
	}

	return NULL;
}

static struct serv* grab_server_slot(CTX)
{
	int i, n = NSERVS;

	for(i = 0; i < n; i++) {
		struct serv* sv = &ctx->servs[i];

		if(sv->flags & SF_SET)
			continue;

		return sv;
	}

	return NULL;
}

static int check_server(CTX, attr srv)
{
	struct serv* sv;

	attr addr = uc_get(srv, ATTR_ADDR);
	int* pptr = uc_get_int(srv, ATTR_PORT);

	if(!addr || !pptr)
		return -EINVAL;

	byte* ip = uc_payload(addr);
	int alen = uc_paylen(addr);
	int port = *pptr;
	int flags = SF_SET;

	if(alen == 4)
		flags = SF_SET;
	else if(alen == 16)
		flags = SF_SET | SF_IPv6;
	else return -EINVAL;

	if((sv = find_server_slot(ctx, ip, alen, port))) {
		sv->flags &= ~SF_FAIL;
	} else if((sv = grab_server_slot(ctx))) {
		memzero(sv, sizeof(*sv));
		sv->flags = flags;
		sv->port = port;
		memcpy(sv->addr, ip, alen);
	}

	return 0;
}

static void wipe_failed(CTX)
{
	int i, n = NSERVS;
	int current = ctx->current;

	for(i = 0; i < n; i++) {
		struct serv* sv = &ctx->servs[i];
		int flags = sv->flags;

		if(current == i)
			continue;
		if(!(flags & SF_SET))
			continue;
		if(!(flags & (SF_KILL | SF_FAIL)))
			continue;

		memzero(sv, sizeof(*sv));
	}
}

static int got_usable_servers(CTX)
{
	int i, n = NSERVS;

	for(i = 0; i < n; i++) {
		struct serv* sv = &ctx->servs[i];
		int flags = sv->flags;

		if(!(flags & SF_SET))
			continue;
		if(flags & (SF_KILL | SF_FAIL))
			continue;

		return 1;
	}

	return 0;
}

static int cmd_srlist(CTX, CN, MSG)
{
	char cbuf[512];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	report_ping(ctx, &uc);

	return send_reply(ctx, cn, &uc);
}

static int cmd_server(CTX, CN, MSG)
{
	int ret;
	attr at;

	wipe_failed(ctx);

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(uc_is_keyed(at, ATTR_SERVER))
			if((ret = check_server(ctx, at)) < 0)
				return ret;

	if((ctx->state == TS_IDLE) && got_usable_servers(ctx))
		set_timed(ctx, TS_SELECT, 1);

	return 0;
}

static int cmd_retry(CTX, CN, MSG)
{
	int usable = 0;

	for(int i = 0; i < NSERVS; i++) {
		struct serv* sv = &ctx->servs[i];
		int flags = sv->flags;

		if(!(flags & SF_SET))
			continue;
		if(flags & SF_KILL)
			continue;

		sv->flags = flags & ~SF_FAIL;

		usable++;
	};

	if(!usable)
		return -ENOENT;

	set_timed(ctx, TS_SELECT, 1);

	return 0;
}

static int cmd_reset(CTX, CN, MSG)
{
	memzero(ctx->servs, sizeof(ctx->servs));

	stop_service(ctx);

	return 0;
}

static int cmd_force(CTX, CN, MSG)
{
	int state = ctx->state;

	switch(state) {
		case TS_IDLE:
			return -ENOTCONN;
		case TS_PING_SENT:
		case TS_PING_WAIT:
		case TS_POLL_SENT:
			return -EINPROGRESS;
		case TS_POLL_WAIT:
			break;
		default:
			return -EFAULT;
	}

	set_timed(ctx, TS_POLL_WAIT, 1);

	return 0;
}

static const struct cmd {
	int cmd;
	int (*call)(CTX, CN, MSG);
} commands[] = {
	{ CMD_STATUS,  cmd_status  },
	{ CMD_SERVER,  cmd_server  },
	{ CMD_SRLIST,  cmd_srlist  },
	{ CMD_RETRY,   cmd_retry   },
	{ CMD_RESET,   cmd_reset   },
	{ CMD_FORCE,   cmd_force   }
};

static int dispatch_cmd(CTX, CN, MSG)
{
	const struct cmd* cd;
	int cmd = uc_repcode(msg);
	int ret;

	for(cd = commands; cd < ARRAY_END(commands); cd++)
		if(cd->cmd == cmd)
			break;
	if(!cd->cmd)
		ret = reply(ctx, cn, -ENOSYS);
	else if((ret = cd->call(ctx, cn, msg)) <= 0)
		ret = reply(ctx, cn, ret);

	return ret;
}

void check_client(CTX, CN)
{
	int ret, fd = cn->fd;
	struct ucattr* msg;
	char buf[100];

	if((ret = uc_recv(fd, buf, sizeof(buf))) < 0)
		goto err;
	if(!(msg = uc_msg(buf, ret)))
		goto err;
	if((ret = dispatch_cmd(ctx, cn, msg)) >= 0)
		return;
err:
	sys_shutdown(fd, SHUT_RDWR);
}
