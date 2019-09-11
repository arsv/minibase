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
#define MSG struct ucmsg* msg __unused
#define RC struct proc* rc
#define UC struct ucbuf* uc

static int start_long_reply(UC)
{
	int len = PAGE;
	int ret;

	void* brk = origbrk;
	void* end = sys_brk(brk + len);

	memzero(uc, sizeof(*uc));

	if((ret = brk_error(brk, end)))
		return ret;

	uc_buf_set(uc, brk, len);
	uc_put_hdr(uc, 0);

	return 0;
}

static int send_reply(UC, CN)
{
	uc_put_end(uc);

	return uc_send_timed(cn->fd, uc);
}

static int send_long_reply(UC, CN)
{
	int ret = send_reply(uc, cn);

	sys_brk(origbrk);

	return ret;
}

static int reply(CN, int err)
{
	struct ucbuf uc;
	char buf[20];

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, err);

	return send_reply(&uc, cn);
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
	struct proc* rc;
	struct ucbuf uc;
	int ret;

	if((ret = start_long_reply(&uc)))
		return ret;

	for(rc = firstrec(); rc; rc = nextrec(rc)) {
		struct ucattr* at;

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

	ret = send_long_reply(&uc, cn);

	return ret;
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

static void put_ring_buf(struct ucbuf* uc, struct proc* rc)
{
	int ptr = rc->ptr;
	char* buf = ring_buf_for(rc);

	if(rc->ptr <= RINGSIZE) {
		uc_put_bin(uc, ATTR_RING, buf, ptr);
		return;
	}

	int tail = ptr % RINGSIZE;
	int head = RINGSIZE - tail;
	void* payload;

	if(!(payload = uc_put_attr(uc, ATTR_RING, RINGSIZE)))
		return;

	memcpy(payload, buf + tail, head);
	memcpy(payload + head, buf, tail);
}

static int cmd_status(CN, MSG, RC)
{
	struct ucbuf uc;
	int ret;

	if((ret = start_long_reply(&uc)) < 0)
		return ret;

	uc_put_str(&uc, ATTR_NAME, rc->name);

	if(rc->pid > 0)
		uc_put_int(&uc, ATTR_PID, rc->pid);
	if(rc->ptr)
		put_ring_buf(&uc, rc);

	if(rc->lastrun)
		uc_put_int(&uc, ATTR_TIME, runtime(rc));
	if(rc->status && !(rc->flags & P_DISABLED))
		uc_put_int(&uc, ATTR_EXIT, rc->status);

	ret = send_long_reply(&uc, cn);

	return ret;
}

static int cmd_getpid(CN, MSG, RC)
{
	struct ucbuf uc;
	char buf[50];

	if(rc->pid <= 0)
		return -ECHILD;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_int(&uc, ATTR_PID, rc->pid);

	return send_reply(&uc, cn);
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
	{ CMD_GETPID,   cmd_getpid   },
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
	{ 0,            NULL         }
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
	int cmd = msg->cmd;

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
	struct ucmsg* msg;
	char buf[200];

	if((ret = uc_recv_whole(fd, buf, sizeof(buf))) < 0)
		goto err;
	if(!(msg = uc_msg(buf, ret)))
		goto err;
	if((ret = dispatch_cmd(cn, msg)) > 0)
		return; /* replied already */
	if((ret = reply(cn, ret)) >= 0)
		return; /* code-only reply successful */
err:
	sys_shutdown(fd, SHUT_RDWR);
}

void accept_ctrl(int sfd)
{
	int cfd;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	int flags = SOCK_NONBLOCK;
	struct conn *cn;

	while((cfd = sys_accept4(sfd, &addr, &addr_len, flags)) > 0) {
		if((cn = grab_conn_slot())) {
			cn->fd = cfd;
		} else {
			sys_shutdown(cfd, SHUT_RDWR);
			sys_close(cfd);
		}
	}

	request(F_UPDATE_PFDS);
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
		return report("socket", "AF_UNIX", fd);

	long ret;
	char* name = addr.path;

	ctrlfd = fd;
	request(F_UPDATE_PFDS);

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		report("bind", name, ret);
	else if((ret = sys_listen(fd, 1)))
		report("listen", name, ret);
	else
		return;

	ctrlfd = -1;
	sys_close(fd);
}
