#include <bits/socket/unix.h>
#include <sys/open.h>
#include <sys/read.h>
#include <sys/socket.h>
#include <sys/connect.h>

#include <nlusctl.h>
#include <format.h>
#include <util.h>
#include <heap.h>
#include <fail.h>

#include "config.h"
#include "common.h"

ERRTAG = "wictl";
ERRLIST = {
	REPORT(ENOENT), REPORT(EINVAL), REPORT(ENOSYS), REPORT(ENOENT),
	RESTASNUMBERS
};

struct top {
	int fd;
	struct heap hp;
	struct ucbuf tx;
	int socket;
	char cbuf[128];
};

#define OPTS "ewsipzn"
#define OPT_e (1<<0)
#define OPT_w (1<<1)
#define OPT_s (1<<2)
#define OPT_p (1<<4)
#define OPT_z (1<<5)

static int heap_left(struct top* ctx)
{
	return ctx->hp.end - ctx->hp.ptr;
}

static void send_command(struct top* ctx)
{
	int wr, fd = ctx->fd;
	char* txbuf = ctx->tx.brk;
	int txlen = ctx->tx.ptr - ctx->tx.brk;

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

	return msg;
}

static struct ucmsg* send_recv(struct top* ctx)
{
	send_command(ctx);
	return recv_reply(ctx);
}

static struct ucmsg* send_check(struct top* ctx)
{
	struct ucmsg* msg;

	msg = send_recv(ctx);

	if(msg->cmd < 0)
		fail(NULL, NULL, msg->cmd);

	return msg;
}

static void cmd_status(struct top* ctx)
{
	struct ucbuf* uc = &ctx->tx;

	uc_put_hdr(uc, CMD_STATUS);
	uc_put_end(uc);

	struct ucmsg* msg = send_check(ctx);

	uc_dump(msg);
}

static void top_init(struct top* ctx)
{
	int fd, ret;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = WICTL
	};

	hinit(&ctx->hp, PAGE);
	uc_buf_set(&ctx->tx, ctx->cbuf, sizeof(ctx->cbuf));

	if((fd = syssocket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", NULL, fd);
	if((ret = sysconnect(fd, &addr, sizeof(addr))) < 0)
		fail("connect", NULL, ret);

	ctx->fd = fd;
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;
	struct top ctx;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(opts)
		fail("unsupported opts", NULL, 0);

	top_init(&ctx);
	cmd_status(&ctx);

	return 0;
}
