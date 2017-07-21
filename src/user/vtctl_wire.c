#include <bits/socket/unix.h>
#include <sys/brk.h>
#include <sys/file.h>
#include <sys/socket.h>

#include <string.h>
#include <util.h>
#include <fail.h>

#include "config.h"
#include "vtctl.h"

/* Socket init is split in two parts: socket() call is performed early so
   that it could be used to resolve netdev names into ifis, but connection
   is delayed until send_command() to avoid waking up wimon and then dropping
   the connection because of a local error. */

void init_socket(CTX)
{
	int fd;

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", "AF_UNIX", fd);

	ctx->fd = fd;
}

static void connect_socket(CTX)
{
	int ret;

	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = CONTROL
	};

	if((ret = sys_connect(ctx->fd, &addr, sizeof(addr))) < 0)
		fail("connect", addr.path, ret);

	ctx->connected = 1;
}

void start_request(CTX, int cmd, int count, int paylen)
{
	void* brk;
	int len = sizeof(ctx->smallbuf);

	brk = ctx->smallbuf;

	ctx->uc.brk = brk;
	ctx->uc.ptr = brk;
	ctx->uc.end = brk + len;

	uc_put_hdr(&ctx->uc, cmd);
}

void add_str_attr(CTX, int key, char* val)
{
	uc_put_str(&ctx->uc, key, val);
}

void add_int_attr(CTX, int key, int val)
{
	uc_put_int(&ctx->uc, key, val);
}

void send_request(CTX)
{
	int wr, fd = ctx->fd;
	char* txbuf = ctx->uc.brk;
	int txlen = ctx->uc.ptr - ctx->uc.brk;

	if(!txlen)
		fail("trying to send an empty message", NULL, 0);

	uc_put_end(&ctx->uc);

	if(!ctx->connected)
		connect_socket(ctx);

	if((wr = writeall(fd, txbuf, txlen)) < 0)
		fail("write", NULL, wr);

	memzero(&ctx->uc, sizeof(ctx->uc));
}

static void init_small_rxbuf(CTX)
{
	if(ctx->uc.brk == ctx->smallbuf)
		fail("smallbuf tx-locked", NULL, 0);

	void* buf = ctx->smallbuf;
	int len = sizeof(ctx->smallbuf);

	ctx->ur.buf = buf;
	ctx->ur.mptr = buf;
	ctx->ur.rptr = buf;
	ctx->ur.end = buf + len;
}

struct ucmsg* recv_reply(CTX)
{
	int ret;

	if(!ctx->ur.buf)
		init_small_rxbuf(ctx);

	while((ret = uc_recv(ctx->fd, &ctx->ur, !0)) < 0)
		fail("recv", NULL, ret);

	uc_dump(ctx->ur.msg);

	return ctx->ur.msg;
}
