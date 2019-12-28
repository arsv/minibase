#include <bits/socket.h>
#include <bits/socket/unix.h>

#include <sys/file.h>
#include <sys/mman.h>
#include <sys/signal.h>
#include <sys/socket.h>

#include <format.h>
#include <nlusctl.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "svchub.h"

#define REPLIED 1

#define CN struct conn* cn __unused
#define MSG struct ucattr* msg __unused
#define RC struct proc* rc

static int send_reply(CN, struct ucbuf* uc)
{
	int ret, fd = cn->fd;

	if((ret = uc_wait_writable(fd)) < 0)
		return ret;

	if((ret = uc_send(fd, uc)) < 0)
		return ret;

	return REPLIED;
}

static int send_multi(CTX, CN, struct iovec* iov, int iovcnt)
{
	int ret, fd = cn->fd;

	if((ret = uc_wait_writable(fd)) < 0)
		return ret;
	if((ret = uc_send_iov(fd, iov, iovcnt)) > 0)
		return ret;

	close_conn(ctx, cn);

	return REPLIED;
}

static int reply(CN, int err)
{
	struct ucbuf uc;
	char buf[20];

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, err);

	return send_reply(cn, &uc);
}

/* Notifcations */

static void notify(CTX, struct conn* cn, struct ucbuf* uc)
{
	int fd, ret;

	if((fd = cn->fd) < 0)
		return;

	if((ret = uc_send(fd, uc)) > 0)
		return;
	if(ret != -EAGAIN)
		goto drop;
	if((ret = uc_wait_writable(fd)) < 0)
		goto drop;
	if((ret = uc_send(fd, uc)) > 0)
		return;
drop:
	close_conn(ctx, cn);
}

void notify_dead(CTX, int pid)
{
	struct conn* cn = conns;
	struct conn* ce = conns + ctx->nconns;

	struct ucbuf uc;
	char buf[16];

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, REP_DIED);

	for(; cn < ce; cn++) {
		if(cn->pid == pid)
			notify(ctx, cn, &uc);
	}
}

/* Global commands */

static int cmd_list(CTX, CN, MSG)
{
	int nprocs = ctx->nprocs;
	struct ucbuf uc;
	char buf[2048];

	int* next = uc_get_int(msg, ATTR_NEXT);
	int start = next ? *next : 0;
	int maxrec = 128;

	if(start < 0)
		return -EINVAL;
	if(start >= nprocs)
		return 0;

	struct proc* rc = procs + start;
	struct proc* re = procs + nprocs;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, 0);

	for(; rc < re; rc++) {
		struct ucattr* at;

		if(!rc->name[0])
			continue;
		if(uc_space_left(&uc) < maxrec)
			break;

		at = uc_put_nest(&uc, ATTR_PROC);

		uc_put_str(&uc, ATTR_NAME, rc->name);

		if(rc->pid > 0)
			uc_put_int(&uc, ATTR_PID, rc->pid);
		else if(rc->flags & P_STATUS)
			uc_put_int(&uc, ATTR_EXIT, rc->pid & 0xFFFF);

		if(rc->ptr)
			uc_put_flag(&uc, ATTR_RING);

		uc_end_nest(&uc, at);
	}

	if(rc < re)
		uc_put_int(&uc, ATTR_NEXT, rc - procs);

	return send_reply(cn, &uc);
}

static inline int check(int ret)
{
	return ret < 0 ? ret : 0;
}

static int cmd_reboot(CTX, CN, MSG)
{
	return check(stop_into(ctx, "reboot"));
}

static int cmd_shutdown(CTX, CN, MSG)
{
	return check(stop_into(ctx, "shutdown"));
}

static int cmd_poweroff(CTX, CN, MSG)
{
	return check(stop_into(ctx, "poweroff"));
}

static int cmd_reload(CTX, CN, MSG)
{
	return check(reload_procs(ctx));
}

/* Commands working on a single proc entry */

static int cmd_status(CTX, CN, MSG, RC)
{
	char buf[128];
	struct ucbuf uc;
	int pid = rc->pid;

	uc_buf_set(&uc, buf, sizeof(buf));

	uc_put_str(&uc, ATTR_NAME, rc->name);

	if(pid > 0)
		uc_put_int(&uc, ATTR_PID, pid);
	else if(pid < 0)
		uc_put_int(&uc, ATTR_EXIT, pid & 0xFFFF);
	if(rc->ptr)
		uc_put_flag(&uc, ATTR_RING);

	return send_reply(cn, &uc);
}

static int cmd_getbuf(CTX, CN, MSG, RC)
{
	struct ucbuf uc;
	char buf[50];
	struct iovec iov[3];
	int ret, iovcnt;

	int ptr = rc->ptr;
	char* ring = rc->buf;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, 0);

	if(!ring)
		return -ENOENT;

	if((ret = uc_iov_hdr(&iov[0], &uc)) < 0)
		return ret;

	if(ptr <= RINGSIZE) {
		iov[1].base = ring;
		iov[1].len = ptr;
		iovcnt = 2;
	} else {
		int off = ptr % RINGSIZE;
		iov[1].base = ring + off;
		iov[1].len = RINGSIZE - off;
		iov[2].base = ring;
		iov[2].len = off;
		iovcnt = 3;
	}

	return send_multi(ctx, cn, iov, iovcnt);
}

static int kill_proc(CTX, RC, int sig)
{
	int ret, pid = rc->pid;

	if(pid <= 0)
		return -ESRCH;
	if((ret = sys_kill(pid, sig)))
		return ret;

	return 0;
}

static int cmd_start(CTX, CN, MSG, RC)
{
	int ret;

	if((ret = start_proc(ctx, rc)) < 0)
		return ret;

	flush_ring_buf(rc);

	return 0;
}

static int cmd_reset(CTX, CN, MSG, RC)
{
	int ret;

	if((ret = kill_proc(ctx, rc, SIGTERM)) < 0)
		return ret;

	rc->flags |= P_RESTART;
	cn->pid = rc->pid;

	return 0;
}

static int cmd_stop(CTX, CN, MSG, RC)
{
	int ret;

	if((ret = stop_proc(ctx, rc)) < 0)
		return ret;

	cn->pid = rc->pid;

	return 0;
}

static int cmd_flush(CTX, CN, MSG, RC)
{
	return check(flush_ring_buf(rc));
}

static int cmd_hup(CTX, CN, MSG, RC)
{
	return kill_proc(ctx, rc, SIGHUP);
}

static const struct pcmd {
       int cmd;
       int (*call)(CTX, CN, MSG, RC);
} pcommands[] = {
       { CMD_STATUS,   cmd_status   },
       { CMD_GETBUF,   cmd_getbuf   },
       { CMD_START,    cmd_start    },
       { CMD_STOP,     cmd_stop     },
       { CMD_RESET,    cmd_reset    },
       { CMD_FLUSH,    cmd_flush    },
       { CMD_HUP,      cmd_hup      },
};

static const struct gcmd {
       int cmd;
       int (*call)(CTX, CN, MSG);
} gcommands[] = {
       { CMD_LIST,     cmd_list     },
       { CMD_RELOAD,   cmd_reload   },
       { CMD_REBOOT,   cmd_reboot   },
       { CMD_SHUTDOWN, cmd_shutdown },
       { CMD_POWEROFF, cmd_poweroff },
};

static int proc_cmd(CTX, CN, MSG, const struct pcmd* pc)
{
       char* name;
       struct proc* rc;

       if(!(name = uc_get_str(msg, ATTR_NAME)))
               return -EINVAL;
       if(!(rc = find_by_name(ctx, name)))
               return -ENOENT;

       return pc->call(ctx, cn, msg, rc);
}

static int dispatch(CTX, CN, MSG)
{
       const struct pcmd* pc;
       const struct gcmd* gc;
       int cmd = uc_repcode(msg);

       for(pc = pcommands; pc < ARRAY_END(pcommands); pc++)
               if(pc->cmd == cmd)
                       return proc_cmd(ctx, cn, msg, pc);

       for(gc = gcommands; gc < ARRAY_END(gcommands); gc++)
               if(gc->cmd == cmd)
                       return gc->call(ctx, cn, msg);

       return -ENOSYS;
}

void check_conn(CTX, struct conn* cn)
{
	int fd = cn->fd;
	int ret;
	struct ucattr* msg;
	char buf[200];

	if((ret = uc_recv(fd, buf, sizeof(buf))) < 0)
		goto err;
	if(!(msg = uc_msg(buf, ret)))
		goto err;
	if((ret = dispatch(ctx, cn, msg)) > 0)
		return; /* replied already */
	if((ret = reply(cn, ret)) >= 0)
		return; /* code-only reply successful */
err:
	close_conn(ctx, cn);
}

static void update_nconns(CTX)
{
	int n = ctx->nconns;

	while(n > 0) {
		struct conn* cn = &conns[n-1];

		if(cn->fd > 0) break;

		n--;
	}

	ctx->nconns = n;
}

void close_conn(CTX, struct conn* cn)
{
	int fd = cn->fd;

	if(fd < 0) return;

	sys_close(fd);

	cn->fd = -1;
	cn->pid = 0;

	ctx->pollset = 0;

	update_nconns(ctx);
}

static struct conn* grab_conn_slot(CTX)
{
	int nconns = ctx->nconns;
	struct conn* cn = conns;
	struct conn* ce = conns + nconns;

	for(; cn < ce; cn++)
		if(cn->fd < 0)
			return cn;
	if(nconns >= NCONNS)
		return NULL;

	ctx->nconns++;

	return cn;
}

void check_control(CTX)
{
	int sfd = ctx->ctlfd;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	int cfd, flags = SOCK_NONBLOCK;
	struct conn *cn;

	while((cfd = sys_accept4(sfd, &addr, &addr_len, flags)) > 0) {
		if((cn = grab_conn_slot(ctx))) {
			cn->fd = cfd;
			ctx->pollset = 0;
		} else {
			sys_close(cfd);
		}
	}
}

void setup_control(CTX)
{
	int ret, fd;
	char* path = CONTROL;
	const int flags = SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		return quit(ctx, "socket", "AF_UNIX", fd);

	if((ret = uc_listen(fd, path, 5)) < 0)
		return quit(ctx, "ucbind", path, ret);

	ctx->ctlfd = fd;
}
