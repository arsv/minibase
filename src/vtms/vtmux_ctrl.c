#include <bits/socket.h>
#include <bits/socket/unix.h>

#include <sys/socket.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/signal.h>
#include <sys/timer.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "vtmux.h"

#define NOERROR 0
#define REPLIED 1

#define CN struct conn* cn __unused
#define MSG struct ucattr* msg __unused

int ctrlfd;

static int send_reply(CN, struct ucbuf* uc)
{
	int ret, fd = cn->fd;

	if((ret = uc_wait_writable(fd)) < 0)
		return ret;

	return uc_send(fd, uc);
}

static int reply(CN, int err)
{
	char buf[128];
	struct ucbuf uc;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, err);

	return send_reply(cn, &uc);
}

static int cmd_switch(CN, MSG)
{
	int ret, *tty;

	if(!(tty = uc_get_int(msg, ATTR_TTY)))
		return -EINVAL;

	if(switchlock)
		return -EPERM;

	if(*tty == 0)
		ret = show_greeter();
	else
		ret = switchto(*tty);

	return ret > 0 ? 0 : ret;
}

static int cmd_swback(CN, MSG)
{
	int ret;

	if(switchlock)
		return -EPERM;

	if(lastusertty && find_term_by_tty(lastusertty))
		ret = switchto(lastusertty);
	else if(primarytty && find_term_by_tty(primarytty))
		ret = switchto(primarytty);
	else
		ret = -ENOENT;

	return ret > 0 ? 0 : ret;
}

/* No up-directory escapes here. Only basenames */

static int cmd_status(CN, MSG)
{
	struct term* vt;
	struct ucattr* at;
	struct ucbuf uc;
	char buf[512];

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, 0);
	uc_put_int(&uc, ATTR_TTY, activetty);

	for(vt = terms; vt < terms + nterms; vt++) {
		if(!vt->tty)
			continue;

		at = uc_put_nest(&uc, ATTR_VT);

		uc_put_int(&uc, ATTR_TTY, vt->tty);

		if(vt->pid > 0)
			uc_put_int(&uc, ATTR_PID, vt->pid);
		if(vt->graph)
			uc_put_flag(&uc, ATTR_GRAPH);

		uc_end_nest(&uc, at);
	}

	return send_reply(cn, &uc);
}

static int cmd_swlock(CN, MSG)
{
	switchlock = 1;
	return 0;
}

static int cmd_unlock(CN, MSG)
{
	switchlock = 0;
	return 0;
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_STATUS,  cmd_status },
	{ CMD_SWITCH,  cmd_switch },
	{ CMD_SWBACK,  cmd_swback },
	{ CMD_SWLOCK,  cmd_swlock },
	{ CMD_UNLOCK,  cmd_unlock },
};

static int dispatch_cmd(CN, MSG)
{
	const struct cmd* cd;
	int cmd = uc_repcode(msg);
	int ret;

	for(cd = commands; cd < ARRAY_END(commands); cd++)
		if(cd->cmd == cmd)
			goto got;

	return reply(cn, -ENOSYS);
got:
	if((ret = cd->call(cn, msg)) > 0)
		return ret;

	return reply(cn, ret);
}

static void close_conn(struct conn* cn)
{
	sys_close(cn->fd);
	
	cn->fd = -1;

	pollset = 0;
}

void recv_conn(struct conn* cn)
{
	int ret, fd = cn->fd;
	struct ucattr* msg;
	char buf[128];

	if((ret = uc_recv(fd, buf, sizeof(buf))) < 0)
		goto err;
	if(!(msg = uc_msg(buf, ret)))
		goto err;
	if((ret = dispatch_cmd(cn, msg)) >= 0)
		return;
err:
	close_conn(cn);
}

void accept_ctrl(void)
{
	int cfd;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	int flags = SOCK_NONBLOCK;
	struct conn *cn;

	while((cfd = sys_accept4(ctrlfd, &addr, &addr_len, flags)) > 0) {
		if((cn = grab_conn_slot())) {
			cn->fd = cfd;
			pollset = 0;
		} else {
			warn("dropping connection", NULL, 0);
			sys_close(cfd);
		}
	}
}

void setup_ctrl(void)
{
	int fd, ret;
	int flags = SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC;
	char* path = CONTROL;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = uc_listen(fd, path, 10)) < 0)
		fail("ucbind", path, ret);

	ctrlfd = fd;
	pollset = 0;
}
