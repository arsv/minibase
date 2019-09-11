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
#define MSG struct ucmsg* msg __unused

int ctrlfd;

static char rxbuf[200];
static char txbuf[600];
static struct ucbuf uc;

static void start_reply(int cmd)
{
	uc_buf_set(&uc, txbuf, sizeof(txbuf));
	uc_put_hdr(&uc, cmd);
}

static int send_reply(CN)
{
	int ret;

	uc_put_end(&uc);

	if((ret = uc_send_timed(cn->fd, &uc)) < 0)
		return ret;

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

	return ret > 0 ? 0 : ret;
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
	{ CMD_SPAWN,   cmd_spawn  },
	{ CMD_SWBACK,  cmd_swback },
	{ CMD_SWLOCK,  cmd_swlock },
	{ CMD_UNLOCK,  cmd_unlock },
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

void recv_conn(struct conn* cn)
{
	int ret, fd = cn->fd;
	struct ucmsg* msg;

	if((ret = uc_recv_whole(fd, rxbuf, sizeof(rxbuf))) < 0)
		goto err;
	if(!(msg = uc_msg(rxbuf, ret)))
		goto err;
	if((ret = dispatch_cmd(cn, msg)) >= 0)
		return;
err:
	shutdown_conn(cn);
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
			sys_shutdown(cfd, SHUT_RDWR);
			sys_close(cfd);
		}
	}
}

void setup_ctrl(void)
{
	int fd, ret;
	int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = CONTROL
	};

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", addr.path, ret);
	if((ret = sys_listen(fd, 1)))
		quit("listen", addr.path, ret);

	ctrlfd = fd;
	pollset = 0;
}

void clear_ctrl(void)
{
	sys_unlink(CONTROL);
}

void quit(const char* msg, char* arg, int err)
{
	clear_ctrl();
	fail(msg, arg, err);
}
