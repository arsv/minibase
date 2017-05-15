#include <bits/socket/unix.h>
#include <sys/open.h>
#include <sys/read.h>
#include <sys/socket.h>
#include <sys/connect.h>

#include <nlusctl.h>
#include <util.h>
#include <heap.h>
#include <fail.h>

#include "config.h"
#include "wictl.h"

static int heap_left(struct top* ctx)
{
	return ctx->hp.end - ctx->hp.ptr;
}

static void send_command(struct top* ctx)
{
	int wr, fd = ctx->fd;
	char* txbuf = ctx->tx.brk;
	int txlen = ctx->tx.ptr - ctx->tx.brk;

	uc_put_end(&ctx->tx);

	uc_dump((struct ucmsg*)txbuf);

	if((wr = writeall(fd, txbuf, txlen)) < 0)
		fail("write", NULL, wr);
}

static struct ucmsg* recv_reply(struct top* ctx)
{
	int rd, fd = ctx->fd;
	char* rxbuf = ctx->hp.ptr;
	struct ucmsg* msg = NULL;

	while(1) {
		char* rbuf = ctx->hp.ptr;
		int rlen = heap_left(ctx);

		if(rlen < PAGE/4) {
			hextend(&ctx->hp, PAGE);
			rlen = heap_left(ctx);
		};

		if((rd = sysread(fd, rbuf, rlen)) < 0)
			fail("recv", NULL, rd);
		else if(rd == 0)
			break;

		ctx->hp.ptr += rd;

		char* rxend = ctx->hp.ptr + rd;
		int rxlen = rxend - rxbuf;

		if((msg = uc_msg(rxbuf, rxlen)))
			break;
	}

	if(!msg)
		fail("recv", "incomplete message", 0);

	uc_dump(msg);

	return msg;
}

struct ucmsg* send_recv(struct top* ctx)
{
	send_command(ctx);
	return recv_reply(ctx);
}

struct ucmsg* send_check(struct top* ctx)
{
	struct ucmsg* msg;

	msg = send_recv(ctx);

	if(msg->cmd < 0)
		fail(NULL, NULL, msg->cmd);

	return msg;
}

void send_check_empty(struct top* ctx)
{
	struct ucmsg* msg = send_check(ctx);

	if(msg->len > sizeof(msg))
		fail("unexpected reply data", NULL, 0);
}

void top_init(struct top* ctx)
{
	int fd, ret;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = WICTL
	};

	hinit(&ctx->hp, PAGE);
	uc_buf_set(&ctx->tx, ctx->cbuf, sizeof(ctx->cbuf));

	if((fd = syssocket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = sysconnect(fd, &addr, sizeof(addr))) < 0)
		fail("connect", WICTL, ret);

	ctx->fd = fd;
}
