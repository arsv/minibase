#include <bits/socket/unix.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/signal.h>
#include <sys/socket.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>

#include "common.h"
#include "ifmon.h"

#define TIMEOUT 1
#define REPLIED 1
#define LATER 1

#define CN struct conn* cn __unused
#define MSG struct ucmsg* msg __unused

int ctrlfd;

static void send_report(struct ucbuf* uc, int ifi)
{
	struct conn* cn;
	int fd;

	for(cn = conns; cn < conns + nconns; cn++) {
		if(cn->rep != ifi || (fd = cn->fd) <= 0)
			continue;
		if(uc_send_timed(fd, uc) < 0)
			sys_shutdown(fd, SHUT_RDWR);
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

	send_report(&uc, ls->ifi);
}

void report_errno(LS, int err)
{
	char buf[64];
	struct ucbuf uc = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	};

	uc_put_hdr(&uc, REP_IF_DONE);
	uc_put_int(&uc, ATTR_ERRNO, err);
	uc_put_end(&uc);

	send_report(&uc, ls->ifi);
}

void report_exit(LS, int status)
{
	char buf[64];
	struct ucbuf uc = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	};

	uc_put_hdr(&uc, REP_IF_DONE);
	uc_put_int(&uc, ATTR_STATUS, status);
	uc_put_end(&uc);

	send_report(&uc, ls->ifi);
}

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
		uc_put_int(&uc, ATTR_STATE, ls->state);
		uc_end_nest(&uc, at);
	}

	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int cmd_mode(CN, MSG)
{
	struct link* ls;
	int* pi;
	char* mode;
	int ret;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(mode = uc_get_str(msg, ATTR_MODE)))
		return -EINVAL;
	if(!is_marked(*pi))
		return -ENODEV;

	uint mlen = strlen(mode);

	if(mlen > sizeof(ls->mode)-1)
		return -EINVAL;
	if((ls = find_link_slot(*pi)))
		return -EALREADY;
	if(!(ls = grab_link_slot()))
		return -ENOMEM;

	ls->ifi = *pi;
	memcpy(ls->mode, mode, mlen + 1);

	if((ret = update_link_name(ls)) < 0) {
		free_link_slot(ls);
		return ret;
	}

	tracef("link %s mode %s\n", ls->name, ls->mode);

	link_next(ls, LS_SPAWN_MODE);
	cn->ifi = ls->ifi;

	return 0;
}

static int cmd_stop(CN, MSG)
{
	struct link* ls;
	int* pi;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(*pi)))
		return -ENODEV;
	if(ls->flags & LF_SHUTDOWN)
		return -EALREADY;

	tracef("cmd_stop %s\n", ls->name);

	ls->flags |= LF_SHUTDOWN;
	link_next(ls, LS_SPAWN_STOP);
	cn->ifi = ls->ifi;

	return 0;
}

static int cmd_kill(CN, MSG)
{
	struct link* ls;
	int* pi;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(*pi)))
		return -ENODEV;

	int ret, pid = ls->pid;

	if(pid <= 0)
		return -ECHILD;
	if((ret = sys_kill(pid, SIGTERM)) < 0)
		return ret;

	return 0;
}

static int cmd_drop(CN, MSG)
{
	struct link* ls;
	int* pi;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(*pi)))
		return -ENODEV;

	if(ls->pid > 0)
		sys_kill(ls->pid, SIGTERM);

	free_link_slot(ls);

	return 0;
}

static int cmd_dhcp_auto(CN, MSG)
{
	return -ENOSYS;
}

static int cmd_dhcp_once(CN, MSG)
{
	return -ENOSYS;
}

static int cmd_dhcp_stop(CN, MSG)
{
	return -ENOSYS;
}

/* Simulate cable reconnect */

static int cmd_reconnect(CN, MSG)
{
	return -ENOSYS;
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_IF_STATUS,    cmd_status    },
	{ CMD_IF_MODE,      cmd_mode      },
	{ CMD_IF_DROP,      cmd_drop      },
	{ CMD_IF_STOP,      cmd_stop      },
	{ CMD_IF_KILL,      cmd_kill      },
	{ CMD_IF_DHCP_AUTO, cmd_dhcp_auto },
	{ CMD_IF_DHCP_ONCE, cmd_dhcp_once },
	{ CMD_IF_DHCP_STOP, cmd_dhcp_stop },
	{ CMD_IF_RECONNECT, cmd_reconnect }
};

static int dispatch(struct conn* cn, struct ucmsg* msg)
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
	struct ucmsg* msg;

	if((ret = uc_recv_whole(fd, buf, sizeof(buf))) < 0)
		goto err;
	if(!(msg = uc_msg(buf, ret)))
		goto err;
	if((ret = dispatch(cn, msg)) >= 0)
		return;
err:
	sys_shutdown(fd, SHUT_RDWR);
}

void accept_ctrl(void)
{
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn *cn;
	int flags = SOCK_NONBLOCK;
	int sfd = ctrlfd;
	int cfd;

	while((cfd = sys_accept4(sfd, &addr, &addr_len, flags)) > 0) {
		if(!(cn = grab_conn_slot()))
			sys_close(cfd);
		else
			cn->fd = cfd;
	}
}

void setup_control(void)
{
	int fd, ret;
	const int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = IFCTL
	};

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", addr.path, ret);
	if((ret = sys_listen(fd, 1)))
		quit("listen", addr.path, ret);

	ctrlfd = fd;
}
