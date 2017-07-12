#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/kill.h>
#include <sys/itimer.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <alloca.h>
#include <util.h>

#include "common.h"
#include "svmon.h"

#define NPREQS 10

#define NOERROR 0
#define REPLIED 1

#define CN struct conn* cn
#define MSG struct ucmsg* msg

static struct preq {
	char* name;
	struct proc* rc;
} preqs[NPREQS];

static int send_reply(CN, struct ucbuf* uc)
{
	writeall(cn->fd, uc->brk, uc->ptr - uc->brk);
	return REPLIED;
}

static int reply(CN, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);
	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int rep_name_err(CN, int err, char* name)
{
	char cbuf[50];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);
	uc_put_str(&uc, ATTR_NAME, name);
	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int ringsize(struct ring* rg)
{
	if(!rg)
		return 0;
	if(rg->ptr < RINGSIZE)
		return rg->ptr;
	else
		return RINGSIZE;
}

static int estimate_list_size(void)
{
	int count = 0;
	struct proc* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		count++;

	return 10*count*sizeof(struct ucattr) + count*sizeof(*rc);
}

static int estimate_status_size(struct proc* rc, struct ring* rg)
{
	return 5*sizeof(struct ucattr) + sizeof(struct proc) + ringsize(rg);
}

static void put_proc_entry(struct ucbuf* uc, struct proc* rc)
{
	struct ucattr* at;

	at = uc_put_nest(uc, ATTR_PROC);

	uc_put_str(uc, ATTR_NAME, rc->name);

	if(rc->pid > 0)
		uc_put_int(uc, ATTR_PID, rc->pid);
	if(ring_buf_for(rc))
		uc_put_flag(uc, ATTR_RING);

	uc_end_nest(uc, at);
}

static int rep_list(CN)
{
	int size = estimate_list_size();
	char* buf = heap_alloc(size);

	if(!buf) return -ENOMEM;

	struct ucbuf uc = { buf, buf, buf + size, 0 };
	struct proc* rc;

	uc_put_hdr(&uc, 0);

	for(rc = firstrec(); rc; rc = nextrec(rc))
		put_proc_entry(&uc, rc);

	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static void put_ring_buf(struct ucbuf* uc, struct ring* rg)
{
	if(rg->ptr <= RINGSIZE) {
		uc_put_bin(uc, ATTR_RING, rg->buf, rg->ptr);
	} else {
		int tail = rg->ptr % RINGSIZE;
		int head = RINGSIZE - tail;
		struct ucattr* at = uc_put_attr(uc, ATTR_RING, RINGSIZE);
		if(!at) return;
		memcpy(at->payload, rg->buf + tail, head);
		memcpy(at->payload + head, rg->buf, tail);
	}
}

static int rep_status(CN, struct proc* rc)
{
	struct ring* rg = ring_buf_for(rc);
	int size = estimate_status_size(rc, rg);
	char* buf;

	if(size < 200)
		buf = alloca(size);
	else
		buf = heap_alloc(size);
	if(!buf)
		return -ENOMEM;

	struct ucbuf uc = { buf, buf, buf + size, 0 };

	uc_put_hdr(&uc, 0);
	uc_put_str(&uc, ATTR_NAME, rc->name);

	if(rc->pid > 0)
		uc_put_int(&uc, ATTR_PID, rc->pid);
	if(rg)
		put_ring_buf(&uc, rg);

	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int rep_pid(CN, struct proc* rc)
{
	char buf[100];
	struct ucbuf uc = { buf, buf, buf + sizeof(buf), 0 };

	uc_put_hdr(&uc, 0);
	uc_put_int(&uc, ATTR_PID, rc->pid);
	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int reboot(char code)
{
	gg.rbcode = code;
	stop_all_procs();
	return 0;
}

static int cmd_reboot(CN, MSG)
{
	return reboot('r');
}

static int cmd_shutdown(CN, MSG)
{
	return reboot('h');
}

static int cmd_poweroff(CN, MSG)
{
	return reboot('p');
}

static int cmd_list(CN, MSG)
{
	return rep_list(cn);
}

static int cmd_status(CN, MSG)
{
	char* name;
	struct proc* rc;

	if(!(name = uc_get_str(msg, ATTR_NAME)))
		return -EINVAL;
	if(!(rc = find_by_name(name)))
		return -ENOENT;

	return rep_status(cn, rc);
}

static int cmd_getpid(CN, MSG)
{
	char* name;
	struct proc* rc;

	if(!(name = uc_get_str(msg, ATTR_NAME)))
		return -EINVAL;
	if(!(rc = find_by_name(name)))
		return -ENOENT;
	if(rc->pid <= 0)
		return -ECHILD;

	return rep_pid(cn, rc);
}

static int cmd_reload(CN, MSG)
{
	gg.reload = 1;
	return 0;
}

static int foreach_named(CN, MSG, void (*func)(struct proc* rc))
{
	int n = 0;
	struct proc* rc;
	struct preq* pr;
	char* name;
	struct ucattr* at;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at)) {
		if(!(name = uc_is_str(at, ATTR_NAME)))
			continue;
		if(n >= NPREQS)
			return -ENFILE;
		if(!(rc = find_by_name(at->payload)))
			return rep_name_err(cn, -ENOENT, name);

		pr = &preqs[n++];
		pr->name = name;
		pr->rc = rc;
	}

	for(pr = preqs; pr < preqs + n; pr++)
		func(pr->rc);

	return NOERROR;
}

static int forall_procs(CN, MSG, void (*func)(struct proc* rc))
{
	struct proc* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		func(rc);

	return 0;
}

static void disable_proc(struct proc* rc)
{
	rc->lastsig = 0;
	rc->flags |= P_DISABLED;
	gg.passreq = 1;
}

static void enable_proc(struct proc* rc)
{
	rc->lastrun = 0;
	rc->flags &= ~P_DISABLED;
	gg.passreq = 1;
}

static void restart_proc(struct proc* rc)
{
	if(rc->pid > 0)
		sys_kill(rc->pid, SIGTERM);

	if(rc->flags & P_DISABLED) {
		rc->flags &= ~P_DISABLED;
		gg.passreq = 1;
	}

	flush_ring_buf(rc);
}

static void flush_proc(struct proc* rc)
{
	flush_ring_buf(rc);
}

static int cmd_enable(CN, MSG)
{
	return foreach_named(cn, msg, enable_proc);
}

static int cmd_disable(CN, MSG)
{
	return foreach_named(cn, msg, disable_proc);
}

static int cmd_restart(CN, MSG)
{
	return foreach_named(cn, msg, restart_proc);
}

static int cmd_flush(CN, MSG)
{
	if(uc_get(msg, ATTR_NAME))
		return foreach_named(cn, msg, flush_proc);
	else
		return forall_procs(cn, msg, flush_proc);
}

static void kill_proc(struct proc* rc, int group, int sig)
{
	int pid = rc->pid;

	if(pid <= 0)
		return;
	if(group)
		pid = -pid;

	sys_kill(pid, sig);
}

static void pause_proc(struct proc* rc)
{
	kill_proc(rc, 1, SIGSTOP);
}

static void resume_proc(struct proc* rc)
{
	kill_proc(rc, 1, SIGCONT);
}

static void hup_proc(struct proc* rc)
{
	kill_proc(rc, 0, SIGHUP);
}

static int cmd_pause(CN, MSG)
{
	return foreach_named(cn, msg, pause_proc);
}

static int cmd_resume(CN, MSG)
{
	return foreach_named(cn, msg, resume_proc);
}

static int cmd_hup(CN, MSG)
{
	return foreach_named(cn, msg, hup_proc);
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_LIST,     cmd_list     },
	{ CMD_ENABLE,   cmd_enable   },
	{ CMD_DISABLE,  cmd_disable  },
	{ CMD_RESTART,  cmd_restart  },
	{ CMD_REBOOT,   cmd_reboot   },
	{ CMD_RELOAD,   cmd_reload   },
	{ CMD_STATUS,   cmd_status   },
	{ CMD_GETPID,   cmd_getpid   },
	{ CMD_FLUSH,    cmd_flush    },
	{ CMD_PAUSE,    cmd_pause    },
	{ CMD_RESUME,   cmd_resume   },
	{ CMD_HUP,      cmd_hup      },
	{ CMD_SHUTDOWN, cmd_shutdown },
	{ CMD_POWEROFF, cmd_poweroff },
	{ 0,            NULL         }
};

void dispatch_cmd(CN, MSG)
{
	const struct cmd* cd;
	int cmd = msg->cmd;
	int ret;

	for(cd = commands; cd->cmd; cd++)
		if(cd->cmd == cmd)
			break;
	if(!cd->cmd)
		reply(cn, -ENOSYS);
	else if((ret = cd->call(cn, msg)) <= 0)
		reply(cn, ret);

	heap_flush();
}
