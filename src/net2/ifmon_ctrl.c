#include <bits/socket/unix.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/socket.h>
#include <sys/sched.h>

#include <nlusctl.h>
#include <printf.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "ifmon.h"

#define TIMEOUT 1
#define REPLIED 1

#define LS struct link* ls
#define CN struct conn* cn
#define MSG struct ucmsg* msg

static void send_report(char* buf, int len, int ifi)
{
	struct conn* cn;
	int fd;

	for(cn = conns; cn < conns + nconns; cn++) {
		if(cn->rep != ifi || (fd = cn->fd) <= 0)
			continue;

		struct itimerval old, itv = {
			.interval = { 0, 0 },
			.value = { 1, 0 }
		};

		sys_setitimer(ITIMER_REAL, &itv, &old);

		if(sys_write(fd, buf, len) < 0)
			sys_shutdown(fd, SHUT_RDWR);

		sys_setitimer(ITIMER_REAL, &old, NULL);
	}
}

static void report_simple(LS, int cmd)
{
	char buf[64];
	struct ucbuf uc = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	};

	uc_put_hdr(&uc, cmd);
	uc_put_end(&uc);

	send_report(uc.brk, uc.ptr - uc.brk, ls->ifi);
}

void report_link_down(LS)
{
	tracef("%s\n", __FUNCTION__);
	report_simple(ls, REP_IF_LINK_DOWN);
}

void report_link_gone(LS)
{
	tracef("%s\n", __FUNCTION__);
	report_simple(ls, REP_IF_LINK_GONE);
}

void report_link_stopped(LS)
{
	tracef("%s\n", __FUNCTION__);
	report_simple(ls, REP_IF_LINK_STOP);
}

void report_link_dhcp(LS, int status)
{
	tracef("%s %i\n", __FUNCTION__, status);

	if(status) {
		report_simple(ls, REP_IF_DHCP_FAIL);
	} else {
		report_simple(ls, REP_IF_DHCP_DONE);
	}
}

void report_link_enabled(LS)
{
	tracef("%s\n", __FUNCTION__);
	report_simple(ls, REP_IF_LINK_ENABLED);
}

void report_link_carrier(LS)
{
	tracef("%s\n", __FUNCTION__);
	report_simple(ls, REP_IF_LINK_CARRIER);
}

static void send_reply(struct conn* cn, struct ucbuf* uc)
{
	writeall(cn->fd, uc->brk, uc->ptr - uc->brk);
}

int reply(struct conn* cn, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);
	uc_put_end(&uc);

	send_reply(cn, &uc);

	return REPLIED;
}

static int get_ifi(MSG)
{
	int* pi;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;

	return *pi;
}

static struct link* find_link(MSG)
{
	int ifi;

	if((ifi = get_ifi(msg)) < 0)
		return NULL;

	return find_link_slot(ifi);
}

static void reassess_link(LS)
{
	if(ls->mode == LM_SKIP)
		return;

	if(ls->mode != LM_DOWN) {
		if(ls->flags & LF_ENABLED)
			link_enabled(ls);
		else
			enable_iface(ls);
	} else {
		if(ls->flags & LF_ENABLED)
			disable_iface(ls);
	}
}

static int is_stopped(LS)
{
	return !(ls->flags & LF_RUNNING);
}

static int set_link_mode(CN, MSG, int mode)
{
	struct link* ls;
	int ret;

	if((ret = get_ifi(msg)) < 0)
		return -EINVAL;
	if(!(ls = find_link_slot(ret)))
		return -ENODEV;
	if(!is_stopped(ls))
		return -EBUSY;

	ls->mode = mode;
	ls->flags &= ~(LF_STOP | LF_ERROR);
	cn->rep = ls->ifi;

	ret = reply(cn, 0);

	//save_link(ls, conf);

	reassess_link(ls);

	return ret;
}

static int cmd_status(CN, MSG)
{
	return -ENOSYS;
}

static int cmd_stop(CN, MSG)
{
	struct link* ls;
	int ret;

	if((ret = get_ifi(msg)) < 0)
		return -EINVAL;
	if(!(ls = find_link_slot(ret)))
		return -ENODEV;
	if((ret = stop_link(ls)) < 0)
		return ret;

	cn->rep = ls->ifi;

	return 0;
}

static int cmd_restart(CN, MSG)
{
	struct link* ls;
	int ret;

	if((ret = get_ifi(msg)) < 0)
		return -EINVAL;
	if(!(ls = find_link_slot(ret)))
		return -ENODEV;
	if(!is_stopped(ls))
		return -EBUSY;

	ls->flags &= ~(LF_STOP | LF_ERROR);

	ret = reply(cn, 0);

	reassess_link(ls);

	return ret;
}

static int cmd_set_skip(CN, MSG)
{
	return set_link_mode(cn, msg, LM_SKIP);
}

static int cmd_set_down(CN, MSG)
{
	return set_link_mode(cn, msg, LM_DOWN);
}

static int cmd_set_dhcp(CN, MSG)
{
	return set_link_mode(cn, msg, LM_DHCP);
}

static int cmd_set_wifi(CN, MSG)
{
	return set_link_mode(cn, msg, LM_WIFI);
}

static int cmd_xdhcp(CN, MSG)
{
	struct link* ls;

	tracef("%s\n", __FUNCTION__);

	if((ls = find_link(msg)))
		start_dhcp(ls);

	return REPLIED;
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_IF_STATUS,    cmd_status     },
	{ CMD_IF_STOP,      cmd_stop       },
	{ CMD_IF_RESTART,   cmd_restart    },
	{ CMD_IF_SET_SKIP,  cmd_set_skip   },
	{ CMD_IF_SET_DOWN,  cmd_set_down   },
	{ CMD_IF_SET_DHCP,  cmd_set_dhcp   },
	{ CMD_IF_SET_WIFI,  cmd_set_wifi   },
	{ CMD_IF_XDHCP,     cmd_xdhcp      },
	{ 0,                NULL           }
};

static int dispatch_cmd(struct conn* cn, struct ucmsg* msg)
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
	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));
}

void handle_conn(struct conn* cn)
{
	int ret, fd = cn->fd;
	char buf[100];

	struct urbuf ur = {
		.buf = buf,
		.mptr = buf,
		.rptr = buf,
		.end = buf + sizeof(buf)
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

void accept_ctrl(int sfd)
{
	int cfd;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn *cn;

	while((cfd = sys_accept(sfd, &addr, &addr_len)) > 0)
		if((cn = grab_conn_slot()))
			cn->fd = cfd;
		else
			sys_close(cfd);
}

void unlink_ctrl(void)
{
	sys_unlink(IFCTL);
}

void setup_ctrl(void)
{
	int fd;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = IFCTL
	};

	const int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);

	long ret;

	ctrlfd = fd;

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", addr.path, ret);
	else if((ret = sys_listen(fd, 1)))
		fail("listen", addr.path, ret);
	else
		return;

	ctrlfd = -1;
}
