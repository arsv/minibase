#include <bits/socket/unix.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/socket.h>
#include <sys/sched.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "ifmon.h"

#define TIMEOUT 1
#define REPLIED 1

#define CN struct conn* cn __unused
#define MSG struct ucmsg* msg __unused

int ctrlfd;

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

void report_done(LS)
{
	char buf[64];
	struct ucbuf uc = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	};

	uc_put_hdr(&uc, REP_IF_DONE);
	uc_put_end(&uc);

	send_report(uc.brk, uc.ptr - uc.brk, ls->ifi);
}

static int send_reply(struct conn* cn, struct ucbuf* uc)
{
	writeall(cn->fd, uc->brk, uc->ptr - uc->brk);
	return REPLIED;
}

static int assess_link_busy(CN, LS)
{
	int ret;

	if((ret = assess_link(ls)) == -EBUSY)
		cn->rep = ls->ifi;

	return ret;
}

int reply(struct conn* cn, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);
	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int cmd_status(CN, MSG)
{
	char cbuf[512];
	struct ucbuf uc;
	struct link* ls;
	struct ucattr* at;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, 0);

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi)
			continue;

		at = uc_put_nest(&uc, ATTR_LINK);
		uc_put_int(&uc, ATTR_IFI, ls->ifi);
		uc_put_str(&uc, ATTR_NAME, ls->name);
		uc_put_str(&uc, ATTR_MODE, ls->mode);
		uc_end_nest(&uc, at);
	}

	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int cmd_mode(CN, MSG)
{
	struct link* ls;
	int* pi;
	char* name;
	char* mode;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(mode = uc_get_str(msg, ATTR_MODE)))
		return -EINVAL;
	if(!(name = uc_get_str(msg, ATTR_NAME)))
		return -EINVAL;

	uint mlen = strlen(mode);
	uint nlen = strlen(name);

	if(mlen > sizeof(ls->mode)-1)
		return -EINVAL;
	if(nlen > sizeof(ls->name)-1)
		return -EINVAL;
	if((ls = find_link_slot(*pi)))
		return -EALREADY;
	if(!(ls = grab_link_slot()))
		return -ENOMEM;

	ls->ifi = *pi;
	memcpy(ls->mode, mode, mlen + 1);
	memcpy(ls->name, name, nlen + 1);
	ls->needs = LN_SETUP;
	ls->flags = LF_MISNAMED;

	request_link_name(ls);

	return 0;
}

static int cmd_name(CN, MSG)
{
	struct link* ls;
	int* pi;
	char* name;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(name = uc_get_str(msg, ATTR_NAME)))
		return -EINVAL;

	uint nlen = strlen(name);

	if(nlen > sizeof(ls->name)-1)
		return -EINVAL;
	if(!(ls = find_link_slot(*pi)))
		return -ENOENT;

	memcpy(ls->name, name, nlen + 1);
	ls->flags |= LF_MISNAMED;

	request_link_name(ls);

	return 0;
}

static int cmd_stop(CN, MSG)
{
	int* pi;
	struct link* ls;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(*pi)))
		return -ENOENT;

	kill_all_procs(ls);
	ls->needs = 0;

	if(ls->flags & LF_DISCONT)
		ls->needs |= LN_CANCEL;

	ls->flags |= LF_STOP;

	return assess_link_busy(cn, ls);
}

static int cmd_drop(CN, MSG)
{
	int* pi;
	struct link* ls;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(*pi)))
		return -ENOENT;

	if(any_procs_running(ls) || ls->needs)
		return -ENOTEMPTY;

	free_link_slot(ls);

	return 0;
}

static int enable_link_dhcp(CN, MSG, int bits)
{
	int* pi;
	struct link* ls;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(*pi)))
		return -ENOENT;

	int flags = ls->flags;

	if(flags & LF_DHCP)
		return -EALREADY;

	ls->flags |= bits;

	if(!(flags & LF_CARRIER))
		return 0;

	ls->needs |= LN_REQUEST;

	reassess_link(ls);

	return 0;
}

static int cmd_dhcp_auto(CN, MSG)
{
	return enable_link_dhcp(cn, msg, LF_DHCP);
}

static int cmd_dhcp_once(CN, MSG)
{
	return enable_link_dhcp(cn, msg, LF_DHCP | LF_ONCE);
}

static int cmd_dhcp_stop(CN, MSG)
{
	int* pi;
	struct link* ls;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(*pi)))
		return -ENOENT;

	int flags = ls->flags;

	if(!(flags & LF_DHCP))
		return -ENOTCONN;

	ls->flags = (flags & ~(LF_DHCP | LF_ONCE));

	if(flags & LF_REQUEST)
		kill_all_procs(ls);
	else if(flags & LF_DISCONT)
		ls->needs |= LN_CANCEL;

	return assess_link_busy(cn, ls);
}

/* Simulate cable reconnect */

static int cmd_reconnect(CN, MSG)
{
	int* pi;
	struct link* ls;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(*pi)))
		return -ENOENT;

	int flags = ls->flags;
	int needs = ls->needs;

	if(!(flags & LF_DHCP))
		return -ENOTCONN;

	if(flags & LF_REQUEST)
		kill_all_procs(ls);
	if(flags & LF_DISCONT)
		needs |= LN_CANCEL;

	ls->needs = needs | LN_REQUEST;

	return assess_link_busy(cn, ls);
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_IF_STATUS,    cmd_status    },
	{ CMD_IF_MODE,      cmd_mode      },
	{ CMD_IF_NAME,      cmd_name      },
	{ CMD_IF_STOP,      cmd_stop      },
	{ CMD_IF_DROP,      cmd_drop      },
	{ CMD_IF_DHCP_AUTO, cmd_dhcp_auto },
	{ CMD_IF_DHCP_ONCE, cmd_dhcp_once },
	{ CMD_IF_DHCP_STOP, cmd_dhcp_stop },
	{ CMD_IF_RECONNECT, cmd_reconnect }
};

static int dispatch_cmd(struct conn* cn, struct ucmsg* msg)
{
	const struct cmd* cd;
	int cmd = msg->cmd;
	int ret;

	for(cd = commands; cd < ARRAY_END(commands); cd++)
		if(cd->cmd == cmd)
			break;
	if(!cd->cmd)
		ret = reply(cn, -ENOSYS);
	else if((ret = cd->call(cn, msg)) <= 0)
		ret = reply(cn, ret);

	return ret;
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

	sys_setitimer(0, &old, NULL);
}

void accept_ctrl(int sfd)
{
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn *cn;
	int fd;

	while((fd = sys_accept(sfd, &addr, &addr_len)) > 0) {
		if(!(cn = grab_conn_slot())) {
			sys_close(fd);
		} else {
			cn->fd = fd;
			pollset = 0;
		}
	}
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
		quit("listen", addr.path, ret);
	else
		return;

	ctrlfd = -1;
}
