#include <bits/socket/inet.h>
#include <bits/socket/inet6.h>

#include <sys/socket.h>
#include <sys/ppoll.h>
#include <sys/timer.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "timed.h"
#include "common.h"

typedef struct ucattr* attr;

static int send_reply(struct conn* cn, struct ucbuf* uc)
{
	int fd = cn->fd;
	void* buf = uc->brk;
	long len = uc->ptr - uc->brk;

	if(len < 0)
		return -EINVAL;

	struct timespec ts = { 1, 0 };
	struct pollfd pfd = { fd, POLLOUT, 0 };
	int ret;
again:
	if((ret = sys_send(fd, buf, len, 0)) >= len)
		return ret;

	if(ret < 0) {
		if(ret != -EAGAIN)
			return ret;
	} else if(ret) {
		buf += ret;
		len -= ret;
	}

	if((ret = sys_ppoll(&pfd, 1, &ts, NULL)) < 0)
		return ret;
	else if(ret == 0)
		return -ETIMEDOUT;

	if((ret = sys_send(fd, buf, len, 0)) < 0)
		return ret;

	goto again;
}

static int reply(struct conn* cn, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);
	uc_put_end(&uc);

	return send_reply(cn, &uc);
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

static int cmd_status(CTX, CN, MSG)
{
	char cbuf[512];
	struct ucbuf uc;
	int state = ctx->state;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, 0);

	if(state)
		uc_put_int(&uc, ATTR_STATE, state);

	if(state != TS_IDLE)
		uc_put_int(&uc, ATTR_TIMELEFT, time_left(ctx));

	if(ctx->synctime) {
		uc_put_i64(&uc, ATTR_SYNCTIME, ctx->synctime);
		uc_put_i64(&uc, ATTR_OFFSET, ctx->syncdt);
	} if(ctx->polltime != ctx->synctime) {
		uc_put_i64(&uc, ATTR_POLLTIME, ctx->polltime);
	}

	if(ctx->current > 0) {
		struct serv* ss = current(ctx);

		if(ss->flags & SF_IPv6)
			uc_put_bin(&uc, ATTR_ADDR, ss->addr, 16);
		else
			uc_put_bin(&uc, ATTR_ADDR, ss->addr, 4);

		uc_put_int(&uc, ATTR_PORT, ss->port);
	}

	uc_put_end(&uc);

	return send_reply(cn, &uc);
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

	attr addr = uc_sub(srv, ATTR_ADDR);
	int* pptr = uc_sub_int(srv, ATTR_PORT);

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

static int cmd_server(CTX, CN, MSG)
{
	int ret;
	attr at;

	wipe_failed(ctx);

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if((uc_is_nest(at, ATTR_SERVER)))
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

static const struct cmd {
	int cmd;
	int (*call)(CTX, CN, MSG);
} commands[] = {
	{ CMD_TI_STATUS,  cmd_status  },
	{ CMD_TI_SERVER,  cmd_server  },
	{ CMD_TI_RETRY,   cmd_retry   },
	{ CMD_TI_RESET,   cmd_reset   }
};

static int dispatch_cmd(CTX, CN, MSG)
{
	const struct cmd* cd;
	int cmd = msg->cmd;
	int ret;

	for(cd = commands; cd < ARRAY_END(commands); cd++)
		if(cd->cmd == cmd)
			break;
	if(!cd->cmd)
		ret = reply(cn, -ENOSYS);
	else if((ret = cd->call(ctx, cn, msg)) <= 0)
		ret = reply(cn, ret);

	return ret;
}

void check_client(CTX, CN)
{
	int ret, fd = cn->fd;
	char buf[100];

	struct urbuf ur = {
		.buf = buf,
		.mptr = buf,
		.rptr = buf,
		.end = buf + sizeof(buf)
	};

	while(1) {
		if((ret = uc_recv(fd, &ur, 0)) < 0)
			break;
		if((ret = dispatch_cmd(ctx, cn, ur.msg)) < 0)
			break;
	}
}
