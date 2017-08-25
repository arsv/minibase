#include <bits/socket.h>
#include <bits/socket/unix.h>

#include <sys/socket.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/signal.h>
#include <sys/sched.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <alloca.h>
#include <util.h>

#include "common.h"
#include "vtmux.h"

#define NOERROR 0
#define REPLIED 1

#define CN struct conn* cn
#define MSG struct ucmsg* msg

int ctrlfd;

static char rxbuf[200];
static char txbuf[600];
static struct ucbuf uc;

static void start_reply(int cmd)
{
	char* buf = NULL;
	int len;

	buf = txbuf;
	len = sizeof(txbuf);

	uc.brk = buf;
	uc.ptr = buf;
	uc.end = buf + len;

	uc_put_hdr(&uc, cmd);
}

static int send_reply(CN)
{
	uc_put_end(&uc);

	writeall(cn->fd, uc.brk, uc.ptr - uc.brk);

	return REPLIED;
}

static int reply(CN, int err)
{
	start_reply(err);

	return send_reply(cn);
}

static int cmd_switch(CN, MSG)
{
	int ret, *tty;

	if(!(tty = uc_get_int(msg, ATTR_TTY)))
		return -EINVAL;

	if((ret = switchto(*tty)) < 0)
		return ret;

	return reply(cn, 0);
}

/* No up-directory escapes here. Only basenames */

static int name_is_simple(const char* name)
{
	const char* p;

	if(!*name)
		return 0;
	for(p = name; *p; p++)
		if(*p == '/')
			return 0;

	return 1;
}

static int cmd_spawn(CN, MSG)
{
	char* name;
	int tty;

	if(!(name = uc_get_str(msg, ATTR_NAME)))
		return -EINVAL;
	if(!name_is_simple(name))
		return -EACCES;
	if((tty = query_empty_tty()) <= 0)
		return -ENOTTY;

	int ret = spawn(tty, name);

	return reply(cn, ret);
}

static int cmd_status(CN, MSG)
{
	struct term* vt;
	struct ucattr* at;

	start_reply(0);

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

	return send_reply(cn);
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_STATUS,  cmd_status },
	{ CMD_SWITCH,  cmd_switch },
	{ CMD_SPAWN,   cmd_spawn  },
	{ 0,           NULL       }
};

static int dispatch_cmd(CN, MSG)
{
	const struct cmd* cd;
	int cmd = msg->cmd;
	int ret;

	for(cd = commands; cd->cmd; cd++)
		if(cd->cmd == cmd)
			break;
	if(!cd->cmd)
		ret = reply(cn, -ENOSYS);
	else if((ret = cd->call(cn, msg)) <= 0)
		ret = reply(cn, ret);

	return ret;
}

static void shutdown_conn(struct conn* cn)
{
	sys_shutdown(cn->fd, SHUT_RDWR);
}

void handle_conn(struct conn* cn)
{
	int ret, fd = cn->fd;

	struct urbuf ur = {
		.buf = rxbuf,
		.mptr = rxbuf,
		.rptr = rxbuf,
		.end = rxbuf + sizeof(rxbuf)
	};
	struct itimerval old, itv = {
		.interval = { 0, 0 },
		.value = { 1, 0 }
	};

	sys_setitimer(0, &itv, &old);

	while(1) {
		if((ret = uc_recv(fd, &ur, 0)) < 0)
			break;

		if((ret = dispatch_cmd(cn, ur.msg)) < 0)
			break;
	}

	if(ret < 0 && ret != -EBADF && ret != -EAGAIN)
		shutdown_conn(cn);

	sys_setitimer(0, &old, NULL);
}

void accept_ctrl(void)
{
	int cfd;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn *cn;

	while((cfd = sys_accept(ctrlfd, &addr, &addr_len)) > 0) {
		if((cn = grab_conn_slot())) {
			cn->fd = cfd;
		} else {
			sys_shutdown(cfd, SHUT_RDWR);
			sys_close(cfd);
		}
	}

	pollset = 0;
}

void setup_ctrl(void)
{
	int fd;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = CONTROL
	};
	const int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		return fail("socket", "AF_UNIX", fd);

	long ret;
	char* name = addr.path;

	ctrlfd = fd;
	pollset = 0;

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", name, ret);
	else if((ret = sys_listen(fd, 1)))
		fail("listen", name, ret);
	else
		return;

	ctrlfd = -1;
	sys_close(fd);
}

void clear_ctrl(void)
{
	sys_unlink(CONTROL);
}
