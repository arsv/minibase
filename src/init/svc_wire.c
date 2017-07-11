#include <bits/socket/unix.h>
#include <sys/file.h>
#include <sys/socket.h>

#include <util.h>
#include <fail.h>

#include "config.h"
#include "svc.h"

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
		.path = SVCTL
	};

	if((ret = sys_connect(ctx->fd, &addr, sizeof(addr))) < 0)
		fail("connect", SVCTL, ret);

	ctx->connected = 1;
}

void init_recv_small(CTX)
{
	ctx->ur.buf = ctx->cbuf;
	ctx->ur.mptr = ctx->ur.buf;
	ctx->ur.rptr = ctx->ur.buf;
	ctx->ur.end = ctx->cbuf + sizeof(ctx->cbuf);
}

void init_recv_heap(CTX)
{
	hinit(&ctx->hp, PAGE);

	ctx->ur.buf = ctx->hp.brk;
	ctx->ur.mptr = ctx->ur.buf;
	ctx->ur.rptr = ctx->ur.buf;
	ctx->ur.end = ctx->hp.end;
}

void send_command(CTX)
{
	int wr, fd = ctx->fd;
	char* txbuf = ctx->uc.brk;
	int txlen = ctx->uc.ptr - ctx->uc.brk;

	uc_put_end(&ctx->uc);

	if(!ctx->connected)
		connect_socket(ctx);

	if((wr = writeall(fd, txbuf, txlen)) < 0)
		fail("write", NULL, wr);
}

struct ucmsg* recv_reply(CTX)
{
	int ret;

	if(!(ctx->ur.buf))
		fail("recv w/o buffer", NULL, 0);

	while((ret = uc_recv(ctx->fd, &ctx->ur, !0)) < 0) {
		if(ret != -ENOBUFS)
			fail("recv", NULL, ret);

		if(ctx->ur.buf == ctx->cbuf)
			fail("cannot extend small buf", NULL, 0);
		if(ctx->ur.end != ctx->hp.ptr)
			fail("heap misallocation trap", NULL, 0);

		hextend(&ctx->hp, PAGE);
		ctx->ur.end = ctx->hp.end;
	}

	if(ctx->ur.end == ctx->hp.end)
		ctx->hp.ptr = ctx->ur.rptr;

	return ctx->ur.msg;
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
