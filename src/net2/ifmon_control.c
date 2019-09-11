#include <bits/socket/unix.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/signal.h>
#include <sys/socket.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "ifmon.h"

#define TIMEOUT 1
#define REPLIED 1
#define LATER 1

#define CN struct conn* cn __unused
#define MSG struct ucmsg* msg __unused

static void send_report(CTX, LS, struct ucbuf* uc)
{
	struct conn* conns = ctx->conns;
	int nconns = ctx->nconns;
	struct conn* cn;
	int ifi = ls->ifi;
	int fd;

	for(cn = conns; cn < conns + nconns; cn++) {
		if(cn->ifi != ifi || (fd = cn->fd) <= 0)
			continue;
		if(uc_send_timed(fd, uc) < 0)
			sys_shutdown(fd, SHUT_RDWR);
	}
}

static void report(CTX, LS, int cmd, int key, int val)
{
	char buf[64];
	struct ucbuf uc;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, cmd);
	if(key && val) uc_put_int(&uc, key, val);
	uc_put_end(&uc);

	send_report(ctx, ls, &uc);
}

void report_mode_errno(CTX, LS, int err)
{
	report(ctx, ls, REP_IF_MODE, ATTR_ERRNO, err);
}

void report_mode_exit(CTX, LS, int code)
{
	report(ctx, ls, REP_IF_MODE, ATTR_XCODE, code);
}

void report_stop_errno(CTX, LS, int err)
{
	report(ctx, ls, REP_IF_STOP, ATTR_ERRNO, err);
}

void report_stop_exit(CTX, LS, int code)
{
	report(ctx, ls, REP_IF_STOP, ATTR_XCODE, code);
}

static int send_reply(struct conn* cn, struct ucbuf* uc)
{
	int ret;

	if((ret = uc_send_timed(cn->fd, uc)) < 0)
		return ret;

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

static int cmd_status(CTX, CN, MSG)
{
	char cbuf[2048];
	struct ucbuf uc;
	struct link* ls;
	struct ucattr* at;

	struct link* links = ctx->links;
	int nlinks = ctx->nlinks;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, 0);

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi)
			continue;

		at = uc_put_nest(&uc, ATTR_LINK);

		uc_put_int(&uc, ATTR_IFI, ls->ifi);
		uc_put_int(&uc, ATTR_FLAGS, ls->flags);

		if(ls->name[0])
			uc_put_str(&uc, ATTR_NAME, ls->name);
		if(ls->mode[0])
			uc_put_str(&uc, ATTR_MODE, ls->mode);

		if(ls->flags & LF_RUNNING)
			uc_put_int(&uc, ATTR_PID, ls->pid);
		else if(ls->pid)
			uc_put_int(&uc, ATTR_XCODE, ls->pid);

		uc_end_nest(&uc, at);
	}

	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int cmd_mode(CTX, CN, MSG)
{
	struct link* ls;
	int* pi;
	char* mode;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(mode = uc_get_str(msg, ATTR_MODE)))
		return -EINVAL;

	uint mlen = strlen(mode);

	if(!mlen || mlen > sizeof(ls->mode))
		return -EINVAL;
	if(!(ls = find_link_slot(ctx, *pi)))
		return -ENODEV;
	if(ls->mode[0])
		return -EBUSY;

	memzero(ls->mode, sizeof(ls->mode));
	memcpy(ls->mode, mode, mlen);

	ls->flags &= ~LF_FAILED;
	ls->flags |= LF_NEED_MODE | LF_MARKED;
	cn->ifi = ls->ifi;

	return 0;
}

static int cmd_stop(CTX, CN, MSG)
{
	struct link* ls;
	int* pi;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(ctx, *pi)))
		return -ENODEV;
	if(!(ls->mode[0]))
		return -ESRCH;

	int flags = ls->flags;

	if(!(flags & LF_RUNNING))
		;
	else if((flags & LS_MASK) == LS_DHCP)
		sys_kill(ls->pid, SIGINT);
	else
		sys_kill(ls->pid, SIGTERM);

	cn->ifi = ls->ifi;
	ls->flags &= ~(LF_FAILED | LF_AUTO_DHCP);
	ls->flags &= ~(LF_NEED_MODE);
	ls->flags |= LF_NEED_STOP | LF_MARKED;

	return 0;
}

static int cmd_kill(CTX, CN, MSG)
{
	struct link* ls;
	int* pi;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(ctx, *pi)))
		return -ENODEV;

	int ret;

	if((ls->flags & LF_RUNNING) <= 0)
		return -ECHILD;
	if((ret = sys_kill(ls->pid, SIGTERM)) < 0)
		return ret;

	return 0;
}

static int cmd_drop(CTX, CN, MSG)
{
	struct link* ls;
	int* pi;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(ctx, *pi)))
		return -ENODEV;

	if(ls->flags & LF_RUNNING)
		sys_kill(ls->pid, SIGTERM);

	ls->flags &= ~(LF_ENABLED | LF_CARRIER);
	ls->pid = 0;
	memzero(ls->mode, sizeof(ls->mode));

	return 0;
}

static int cmd_dhcp_auto(CTX, CN, MSG)
{
	int* pi;
	struct link* ls;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(ctx, *pi)))
		return -ENODEV;

	int flags = ls->flags;

	flags |= LF_AUTO_DHCP | LF_MARKED;

	if(flags & LF_CARRIER)
		flags |= LF_NEED_DHCP;

	ls->flags = flags;

	return 0;
}

static int cmd_dhcp_stop(CTX, CN, MSG)
{
	int* pi;
	struct link* ls;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(ctx, *pi)))
		return -ENODEV;

	ls->flags &= ~LF_AUTO_DHCP;

	sighup_running_dhcp(ls);

	return 0;
}

static int cmd_reconnect(CTX, CN, MSG)
{
	struct link* ls;
	int* pi;

	if(!(pi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;
	if(!(ls = find_link_slot(ctx, *pi)))
		return -ENODEV;

	simulate_reconnect(ctx, ls);

	return 0;
}

static const struct cmd {
	int cmd;
	int (*call)(CTX, CN, MSG);
} commands[] = {
	{ CMD_IF_STATUS,    cmd_status    },
	{ CMD_IF_MODE,      cmd_mode      },
	{ CMD_IF_DROP,      cmd_drop      },
	{ CMD_IF_STOP,      cmd_stop      },
	{ CMD_IF_KILL,      cmd_kill      },
	{ CMD_IF_DHCP_AUTO, cmd_dhcp_auto },
	{ CMD_IF_DHCP_STOP, cmd_dhcp_stop },
	{ CMD_IF_RECONNECT, cmd_reconnect }
};

static int dispatch(CTX, CN, MSG)
{
	const struct cmd* cd;
	int cmd = msg->cmd;
	int ret;

	for(cd = commands; cd < ARRAY_END(commands); cd++)
		if(cd->cmd == cmd)
			break;
	if(!cd->cmd)
		ret = reply(cn, -ENOSYS);
	else if((ret = cd->call(ctx, cn, msg)) <= 0)
		ret = reply(cn, ret);

	return ret;
}

void handle_conn(CTX, CN)
{
	int ret, fd = cn->fd;
	char buf[100];
	struct ucmsg* msg;

	if((ret = uc_recv_whole(fd, buf, sizeof(buf))) < 0)
		goto err;
	if(!(msg = uc_msg(buf, ret)))
		goto err;
	if((ret = dispatch(ctx, cn, msg)) >= 0)
		return;
err:
	sys_shutdown(fd, SHUT_RDWR);
}

struct conn* grab_conn_slot(CTX)
{
	struct conn* conns = ctx->conns;
	struct conn* cn;
	int nconns = ctx->nconns;

	for(cn = conns; cn < conns + nconns; cn++)
		if(cn->fd < 0)
			return cn;

	if(nconns >= NCONNS)
		return NULL;

	ctx->nconns = nconns + 1;

	return &conns[nconns];
}

void accept_ctrl(CTX)
{
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn *cn;
	int flags = SOCK_NONBLOCK;
	int sfd = ctx->ctrlfd;
	int cfd;

	while((cfd = sys_accept4(sfd, &addr, &addr_len, flags)) > 0) {
		if(!(cn = grab_conn_slot(ctx)))
			sys_close(cfd);
		else
			cn->fd = cfd;
	}
}

void close_conn(CTX, struct conn* cn)
{
	struct conn* conns = ctx->conns;
	int nconns = ctx->nconns;

	sys_close(cn->fd);

	if(nconns <= 0)
		return; /* should never happen */

	int i = nconns - 1;

	cn->fd = -1;

	if(cn != conns + i) /* non-last entry */
		return;

	while(i >= 0)
		if(conns[i].fd >= 0)
			break;
		else i--;

	ctx->nconns = i + 1;
}

void setup_control(CTX)
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

	ctx->ctrlfd = fd;
}
