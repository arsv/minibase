#include <bits/socket/unix.h>
#include <sys/file.h>
#include <sys/socket.h>

#include <nlusctl.h>
#include <printf.h>
#include <util.h>
#include <heap.h>

#include "common.h"
#include "wifi.h"

/* Socket init is split in two parts: socket() call is performed early so
   that it could be used to resolve netdev names into ifis, but connection
   is delayed until send_command() to avoid waking up wimon and then dropping
   the connection because of a local error. */

void init_heap_socket(CTX)
{
	int fd;

	hinit(&ctx->hp, 2*PAGE);

	char* ucbuf = halloc(&ctx->hp, 2048);

	ctx->uc.brk = ucbuf;
	ctx->uc.ptr = ucbuf;
	ctx->uc.end = ucbuf + 2048;

	char* rxbuf = halloc(&ctx->hp, 2048);

	ctx->ur.buf = rxbuf;
	ctx->ur.mptr = rxbuf;
	ctx->ur.rptr = rxbuf;
	ctx->ur.end = rxbuf + 2048;

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", "AF_UNIX", fd);

	ctx->fd = fd;
}

static int connctl(CTX, struct sockaddr_un* addr, int miss)
{
	int ret;

	if((ret = sys_connect(ctx->fd, addr, sizeof(*addr))) < 0) {
		if(ret != -ENOENT || !miss)
			fail("connect", addr->path, ret);
		else
			return ret;
	}

	ctx->connected = 1;

	return ret;
}

static void resocket(CTX)
{
	int fd;

	sys_close(ctx->fd);

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", "AF_UNIX", fd);

	ctx->fd = fd;
	ctx->connected = 0;
}

void connect_ifctl(CTX)
{
	struct sockaddr_un ifctl = { .family = AF_UNIX, .path = IFCTL };

	connctl(ctx, &ifctl, 0);
}

void connect_wictl(CTX)
{
	struct sockaddr_un wictl = { .family = AF_UNIX, .path = WICTL };

	connctl(ctx, &wictl, 0);
}

void connect_start(CTX)
{
	struct sockaddr_un wictl = { .family = AF_UNIX, .path = WICTL };

	if(connctl(ctx, &wictl, 1) >= 0)
		return;

	connect_ifctl(ctx);

	try_start_wienc(ctx);

	resocket(ctx);

	connect_wictl(ctx);
}

void send_command(CTX)
{
	int wr, fd = ctx->fd;
	char* txbuf = ctx->uc.brk;
	int txlen = ctx->uc.ptr - ctx->uc.brk;

	if(!ctx->connected)
		fail("socket not connected", NULL, 0);

	if((wr = writeall(fd, txbuf, txlen)) < 0)
		fail("write", NULL, wr);
}

struct ucmsg* recv_reply(CTX)
{
	struct urbuf* ur = &ctx->ur;
	int ret, fd = ctx->fd;

	if((ret = uc_recv(fd, ur, 1)) < 0)
		return NULL;

	return ur->msg;
}

struct ucmsg* send_recv_msg(CTX)
{
	struct ucmsg* msg;

	send_command(ctx);

	while((msg = recv_reply(ctx)))
		if(msg->cmd <= 0)
			return msg;

	fail("connection lost", NULL, 0);
}

int send_recv_cmd(CTX)
{
	struct ucmsg* msg = send_recv_msg(ctx);

	return msg->cmd;
}

void send_check(CTX)
{
	int ret;

	if((ret = send_recv_cmd(ctx)) < 0)
		fail(NULL, NULL, ret);
}

void init_output(CTX)
{
	int len = 2048;

	ctx->bo.fd = STDOUT;
	ctx->bo.buf = halloc(&ctx->hp, len);
	ctx->bo.len = len;
	ctx->bo.ptr = 0;
}

void fini_output(CTX)
{
	bufoutflush(&ctx->bo);
}

void output(CTX, char* buf, int len)
{
	bufout(&ctx->bo, buf, len);
}
