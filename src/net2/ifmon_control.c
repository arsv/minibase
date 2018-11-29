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

static void report_simple(int ifi, int cmd)
{
	char buf[64];
	struct ucbuf uc = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	};

	uc_put_hdr(&uc, cmd);
	uc_put_end(&uc);

	send_report(uc.brk, uc.ptr - uc.brk, ifi);
}

//static void report_link(LS, int cmd)
//{
//	report_simple(ls->ifi, cmd);
//}

static void report_dhcp(DH, int cmd)
{
	report_simple(dh->ifi, cmd);
}

//void report_link_down(LS)
//{
//	report_link(ls, REP_IF_LINK_DOWN);
//}
//
//void report_link_gone(LS)
//{
//	report_link(ls, REP_IF_LINK_GONE);
//}
//
//void report_link_stopped(LS)
//{
//	report_link(ls, REP_IF_LINK_STOP);
//}
//
void report_dhcp_done(DH)
{
	report_dhcp(dh, REP_IF_DHCP_FAIL);
}

void report_dhcp_fail(DH)
{
	report_dhcp(dh, REP_IF_DHCP_DONE);
}

//void report_link_enabled(LS)
//{
//	report_link(ls, REP_IF_LINK_ENABLED);
//}
//
//void report_link_carrier(LS)
//{
//	report_link(ls, REP_IF_LINK_CARRIER);
//}

static int send_reply(struct conn* cn, struct ucbuf* uc)
{
	writeall(cn->fd, uc->brk, uc->ptr - uc->brk);
	return REPLIED;
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

static struct link* find_link(MSG)
{
	int* pi;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return NULL;

	return find_link_by_id(*pi);
}

//static int common_flags(LS)
//{
//	int val = ls->flags;
//	int out = 0;
//
//	if(val & LF_ENABLED)
//		out |= IF_ENABLED;
//	if(val & LF_CARRIER)
//		out |= IF_CARRIER;
//	if(val & LF_RUNNING)
//		out |= IF_RUNNING;
//	if(val & LF_STOPPING)
//		out |= IF_STOPPING;
//	if(val & LF_ERROR)
//		out |= IF_ERROR;
//	if(val & LF_DHCPFAIL)
//		out |= IF_DHCPFAIL;
//
//	return out;
//}

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
		uc_put_bin(&uc, ATTR_ADDR, ls->mac, sizeof(ls->mac));
		uc_put_str(&uc, ATTR_NAME, ls->name);
		//uc_put_int(&uc, ATTR_MODE, ls->mode);
		uc_end_nest(&uc, at);
	}

	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int cmd_leases(CN, MSG)
{
	return -ENOSYS;
}

static int cmd_tag_only(CN, MSG)
{
	return -ENOSYS;
}

static int cmd_tag_also(CN, MSG)
{
	return -ENOSYS;
}

static int cmd_tag_none(CN, MSG)
{
	return -ENOSYS;
}

static int cmd_dhcp_auto(CN, MSG)
{
	struct link* ls;

	if((ls = find_link(msg)))
		start_dhcp(ls);

	return REPLIED;
}

static int cmd_dhcp_once(CN, MSG)
{
	struct link* ls;

	if((ls = find_link(msg)))
		start_dhcp(ls);

	return REPLIED;
}

static int cmd_dhcp_stop(CN, MSG)
{
	struct link* ls;

	if((ls = find_link(msg)))
		start_dhcp(ls);

	return REPLIED;
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_IF_STATUS,    cmd_status     },
	{ CMD_IF_LEASES,    cmd_leases     },
	{ CMD_IF_TAG_ONLY,  cmd_tag_only   },
	{ CMD_IF_TAG_ALSO,  cmd_tag_also   },
	{ CMD_IF_TAG_NONE,  cmd_tag_none   },
	{ CMD_IF_DHCP_ONCE, cmd_dhcp_once  },
	{ CMD_IF_DHCP_AUTO, cmd_dhcp_auto  },
	{ CMD_IF_DHCP_STOP, cmd_dhcp_stop  },
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
