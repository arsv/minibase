#include <bits/socket/unix.h>
#include <sys/brk.h>
#include <sys/file.h>
#include <sys/socket.h>

#include <string.h>
#include <util.h>
#include <fail.h>

#include "config.h"
#include "svc.h"

void* heap_alloc(CTX, int size)
{
	struct heap* hp = &ctx->hp;

	if(!hp->ptr) {
		void* brk = (void*)sys_brk(NULL);
		hp->brk = brk;
		hp->ptr = brk;
		hp->end = brk;
	}

	void* old = hp->ptr;
	void* new = old + size;

	if(new > hp->end) {
		int need = new - hp->end;
		need += (PAGE - need) % PAGE;
		hp->end = (void*)sys_brk(hp->end + need);
	}

	if(new > hp->end)
		fail("cannot allocate heap", NULL, 0);

	hp->ptr = new;

	return old;
}

void expect_large(CTX)
{
	int outlen = 1024;
	void* outbuf = heap_alloc(ctx, outlen);

	ctx->bo.fd = STDOUT;
	ctx->bo.buf = outbuf;
	ctx->bo.len = outlen;
	ctx->bo.ptr = 0;

	int rxlen = 1024;
	void* rxbuf = heap_alloc(ctx, rxlen);

	ctx->ur.buf = rxbuf;
	ctx->ur.mptr = rxbuf;
	ctx->ur.rptr = rxbuf;
	ctx->ur.end = rxbuf + rxlen;
}

static void extend_urbuf(CTX)
{
	if(ctx->ur.buf == ctx->smallbuf)
		fail("cannot extend small buf", NULL, 0);
	if(ctx->ur.end != ctx->hp.ptr)
		fail("heap misallocation trap", NULL, 0);

	int rxext = 1024;
	void* new = heap_alloc(ctx, rxext);
	void* end = new + rxext;

	ctx->ur.end = end;
}

void flush_output(CTX)
{
	if(!ctx->bo.ptr)
		return;

	bufoutflush(&ctx->bo);
}

void output(CTX, char* buf, int len)
{
	if(ctx->bo.len)
		bufout(&ctx->bo, buf, len);
	else
		writeall(STDOUT, buf, len);
}

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
	int total = sizeof(struct ucmsg)
		+ count*sizeof(struct ucattr)
		+ paylen + 4*count;

	void* brk;
	int len = sizeof(ctx->smallbuf);

	if(total <= len)
		brk = ctx->smallbuf;
	else
		brk = heap_alloc(ctx, (len = total));

	ctx->uc.brk = brk;
	ctx->uc.ptr = brk;
	ctx->uc.end = brk + len;

	uc_put_hdr(&ctx->uc, cmd);
}

void add_str_attr(CTX, int key, char* val)
{
	uc_put_str(&ctx->uc, key, val);
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
		if(ret == -ENOBUFS)
			extend_urbuf(ctx);
		else
			fail("recv", NULL, ret);

	return ctx->ur.msg;
}
