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
#include "svhub.h"

int ctrlfd;

#define NOERROR 0
#define REPLIED 1
#define LATER 1

#define CN struct conn* cn __unused
#define MSG struct ucattr* msg __unused
#define RC struct proc* rc

static int send_reply(CN, struct ucbuf* uc)
{
	int ret, fd = cn->fd;

	if((ret = uc_wait_writable(fd)) < 0)
		return ret;

	return uc_send(fd, uc);
}

static int send_multi(CN, struct iovec* iov, int iovcnt)
{
	int ret, fd = cn->fd;

	if((ret = uc_wait_writable(fd)) < 0)
		return ret;
	if((ret = uc_send_iov(fd, iov, iovcnt)) > 0)
		return ret;

	close_conn(cn);

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

void notify_dead(int pid)
{
	struct conn* cn;

	for(cn = conns; cn < conns + nconns; cn++) {
		if(cn->pid != pid)
			continue;

		reply(cn, 0);

		cn->pid = 0;
	}
}

/* Global commands */

static int cmd_list(CN, MSG)
{
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
		if(rc->ptr)
			uc_put_flag(&uc, ATTR_RING);
		if(rc->status && !(rc->flags & P_DISABLED))
			uc_put_int(&uc, ATTR_EXIT, rc->status);

		uc_end_nest(&uc, at);
	}

	if(rc < re)
		uc_put_int(&uc, ATTR_NEXT, rc - procs);

	return send_reply(cn, &uc);
}

static int cmd_reboot(CN, MSG)
{
	return stop_into("reboot");
}

static int cmd_shutdown(CN, MSG)
{
	return stop_into("shutdown");
}

static int cmd_poweroff(CN, MSG)
{
	return stop_into("poweroff");
}

static int cmd_reload(CN, MSG)
{
	request(F_RELOAD_PROCS);

	return 0;
}

static int cmd_flushall(CN, MSG)
{
	struct proc* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		flush_ring_buf(rc);

	return 0;
}

/* Commands working on a single proc entry */

static int cmd_status(CN, MSG, RC)
{
	char buf[128];
	struct ucbuf uc;

	uc_buf_set(&uc, buf, sizeof(buf));

	uc_put_str(&uc, ATTR_NAME, rc->name);

	if(rc->pid > 0)
		uc_put_int(&uc, ATTR_PID, rc->pid);
	if(rc->ptr)
		uc_put_flag(&uc, ATTR_RING);

	if(rc->lastrun)
		uc_put_int(&uc, ATTR_TIME, runtime(rc));
	if(rc->status && !(rc->flags & P_DISABLED))
		uc_put_int(&uc, ATTR_EXIT, rc->status);

	return send_reply(cn, &uc);
}

static int cmd_getbuf(CN, MSG, RC)
{
	struct ucbuf uc;
	char buf[50];
	struct iovec iov[3];
	int ret, iovcnt;

	int ptr = rc->ptr;
	char* ring = ring_buf_for(rc);

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, 0);

	if(rc->pid <= 0)
		return -ECHILD;
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

	return send_multi(cn, iov, iovcnt);
}

static int kill_proc(struct proc* rc, int sig)
{
	int ret, pid = rc->pid;

	if(pid <= 0)
		return -ESRCH;
	if((ret = sys_kill(pid, sig)))
		return ret;

	return 0;
}

static int cmd_start(CN, MSG, RC)
{
	if(rc->pid)
		return -EALREADY;

	rc->flags &= ~(P_DISABLED | P_FAILED);

	if(rc->flags & (P_SIGTERM | P_SIGKILL))
		rc->flags |= P_RESTART;

	if(rc->flags & P_STUCK) {
		rc->flags &= ~P_STUCK;
		rc->pid = 0;
	}

	rc->lastrun = 0;
	rc->status = 0;

	request(F_CHECK_PROCS);

	flush_ring_buf(rc);

	return 0;
}

static int cmd_stop(CN, MSG, RC)
{
	if(rc->pid <= 0)
		return -EALREADY;

	rc->lastsig = 0;
	rc->status = 0;

	rc->flags &= ~(P_RESTART | P_FAILED);
	rc->flags |= P_DISABLED;

	request(F_CHECK_PROCS);

	cn->pid = rc->pid;

	return LATER; /* suppress reply */
}

static int cmd_flush(CN, MSG, RC)
{
	if(!rc->ptr)
		return -EALREADY;

	flush_ring_buf(rc);

	return 0;
}

static int cmd_pause(CN, MSG, RC)
{
	return kill_proc(rc, -SIGSTOP);
}

static int cmd_resume(CN, MSG, RC)
{
	return kill_proc(rc, -SIGCONT);
}

static int cmd_hup(CN, MSG, RC)
{
	return kill_proc(rc, SIGHUP);
}

static const struct pcmd {
	int cmd;
	int (*call)(CN, MSG, RC);
} pcommands[] = {
	{ CMD_STATUS,   cmd_status   },
	{ CMD_GETBUF,   cmd_getbuf   },
	{ CMD_START,    cmd_start    },
	{ CMD_STOP,     cmd_stop     },
	{ CMD_PAUSE,    cmd_pause    },
	{ CMD_RESUME,   cmd_resume   },
	{ CMD_HUP,      cmd_hup      },
	{ CMD_FLUSH,    cmd_flush    },
};

static const struct gcmd {
	int cmd;
	int (*call)(CN, MSG);
} gcommands[] = {
	{ CMD_LIST,     cmd_list     },
	{ CMD_RELOAD,   cmd_reload   },
	{ CMD_REBOOT,   cmd_reboot   },
	{ CMD_SHUTDOWN, cmd_shutdown },
	{ CMD_POWEROFF, cmd_poweroff },
	{ CMD_FLUSHALL, cmd_flushall },
};

static int proc_cmd(CN, MSG, const struct pcmd* pc)
{
	char* name;
	struct proc* rc;

	if(!(name = uc_get_str(msg, ATTR_NAME)))
		return -EINVAL;
	if(!(rc = find_by_name(name)))
		return -ENOENT;

	return pc->call(cn, msg, rc);
}

static int dispatch_cmd(CN, MSG)
{
	const struct pcmd* pc;
	const struct gcmd* gc;
	int cmd = uc_repcode(msg);

	for(pc = pcommands; pc < ARRAY_END(pcommands); pc++)
		if(pc->cmd == cmd)
			return proc_cmd(cn, msg, pc);

	for(gc = gcommands; gc < ARRAY_END(gcommands); gc++)
		if(gc->cmd == cmd)
			return gc->call(cn, msg);

	return -ENOSYS;
}

void handle_conn(struct conn* cn)
{
	int fd = cn->fd;
	int ret;
	struct ucattr* msg;
	char buf[200];

	if((ret = uc_recv(fd, buf, sizeof(buf))) < 0)
		goto err;
	if(!(msg = uc_msg(buf, ret)))
		goto err;
	if((ret = dispatch_cmd(cn, msg)) > 0)
		return; /* replied already */
	if((ret = reply(cn, ret)) >= 0)
		return; /* code-only reply successful */
err:
	close_conn(cn);
}

void accept_ctrl(int sfd)
{
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	int cfd, flags = SOCK_NONBLOCK;
	struct conn *cn;

	while((cfd = sys_accept4(sfd, &addr, &addr_len, flags)) > 0) {
		if((cn = grab_conn_slot())) {
			cn->fd = cfd;
		} else {
			sys_close(cfd);
		}
	}

	request(F_UPDATE_PFDS);
}

void setup_ctrl(void)
{
	int ret, fd;
	char* path = CONTROL;
	const int flags = SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		return report("socket", "AF_UNIX", fd);

	if((ret = uc_listen(fd, path, 5)) < 0) {
		report("ucbind", path, ret);
		sys_close(fd);
		ctrlfd = -1;
		return;
	}

	ctrlfd = fd;
	request(F_UPDATE_PFDS);
}
