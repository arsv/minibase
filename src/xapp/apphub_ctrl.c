#include <bits/socket/unix.h>
#include <sys/file.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/mman.h>

#include <string.h>
#include <nlusctl.h>
#include <util.h>

#include "common.h"
#include "apphub.h"

#define MSG struct ucmsg* msg
#define CN struct conn* cn

#define MAX_COMMAND_BUF 8192
#define IOBUF_TIMER 30

#define REPLIED 1

void maybe_drop_iobuf(CTX)
{
	void* buf = ctx->iobuf;
	int len = ctx->iolen;
	int ret;

	if(!buf) return;

	if((ret = sys_munmap(buf, len)) < 0)
		warn("munmap", NULL, ret);

	ctx->iobuf = NULL;
	ctx->iolen = 0;
}

static int prep_recv_buffer(CTX)
{
	void* buf = ctx->iobuf;
	int len = MAX_COMMAND_BUF;
	int ret;
	int proto = PROT_READ | PROT_WRITE;
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;

	if(buf != NULL) {
		/* already allocated; update timer, and keep using it */
		if(ctx->timer == TM_MMAP) {
			ctx->ts.sec = IOBUF_TIMER;
			ctx->ts.nsec = 0;
		}
		return 0;
	}

	buf = sys_mmap(NULL, len, proto, flags, -1, 0);

	if((ret = mmap_error(buf)))
		return ret;

	ctx->iobuf = buf;
	ctx->iolen = len;

	if(ctx->timer == TM_NONE) {
		ctx->timer = TM_MMAP;
		ctx->ts.sec = IOBUF_TIMER;
		ctx->ts.nsec = 0;
	}

	return 0;
}

static int send_reply(struct conn* cn, struct ucbuf* uc)
{
	int ret;

	if((ret = uc_send_timed(cn->fd, uc)) < 0)
		return ret;

	return REPLIED;
}

static int reply(struct conn* cn, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);
	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int ensure_iobuf(CTX, int size)
{
	void* iobuf = ctx->iobuf;
	int iolen = ctx->iolen;

	if(iolen >= size)
		return 0;
	if(!iobuf) /* should not happen */
		return -ENOMEM;

	int need = pagealign(size);
	int flags = MREMAP_MAYMOVE;
	int ret;

	iobuf = sys_mremap(iobuf, iolen, need, flags);
	
	if((ret = mmap_error(iobuf)))
		return ret;

	ctx->iobuf = iobuf;
	ctx->iolen = need;

	return 0;
}

static int cmd_status(CTX, CN, MSG)
{
	int active = ctx->nprocs_nonempty;

	if(!active)
		return 0;

	int est = active*(4+sizeof(struct proc));
	int ret;

	if((ret = ensure_iobuf(ctx, est)) < 0)
		return ret;

	(void)msg;
	struct ucbuf uc;

	uc_buf_set(&uc, ctx->iobuf, ctx->iolen);

	struct proc* pc = ctx->procs;
	struct proc* pe = pc + ctx->nprocs;

	uc_put_hdr(&uc, 0);

	for(; pc < pe; pc++) {
		struct ucattr* at;
		int xid = pc->xid;
		int pid = pc->pid;
		int ptr = pc->ptr;

		if(!xid) continue;

		at = uc_put_nest(&uc, ATTR_PROC);
		uc_put_int(&uc, ATTR_XID, pc->xid);

		if(pid > 0)
			uc_put_int(&uc, ATTR_PID, pid);
		else if(pid < 0)
			uc_put_int(&uc, ATTR_EXIT, pid & 0xFFFF);

		if(ptr > RING_SIZE)
			uc_put_int(&uc, ATTR_RING, RING_SIZE);
		else if(ptr > 0)
			uc_put_int(&uc, ATTR_RING, ptr);

		uc_put_str(&uc, ATTR_NAME, pc->name);
		uc_end_nest(&uc, at);
	}

	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int already_running(CTX, char* name)
{
	struct proc* pc = ctx->procs;
	struct proc* pe = pc + ctx->nprocs;
	int pid;

	for(; pc < pe; pc++)
		if(strncmp(name, pc->name, sizeof(pc->name)))
			continue;
		else if((pid = pc->pid) <= 0)
			continue;
		else
			return pid;

	return 0;
}

static int prep_argv_envp(MSG, char** ptrs, int n)
{
	char** argp = ptrs;
	char** arge = ptrs + n;
	struct ucattr* at;
	int argc;
	char* arg;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(!(arg = uc_is_str(at, ATTR_ARG)))
			continue;
		else if(argp >= arge)
			return -E2BIG;
		else
			*(argp++) = arg;

	if(argp >= arge)
		return -E2BIG;
	if(argp == ptrs)
		return -EINVAL;

	argc = argp - ptrs;
	*(argp++) = NULL;
	
	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(!(arg = uc_is_str(at, ATTR_ENV)))
			continue;
		else if(argp >= arge)
			return -E2BIG;
		else
			*(argp++) = arg;

	if(argp >= arge)
		return -E2BIG;

	*(argp++) = NULL;

	return argc;
}

static int common_spawn(CTX, MSG)
{
	char* args[128];
	int ret;

	if((ret = prep_argv_envp(msg, args, ARRAY_SIZE(args))) < 0)
		return ret;

	char** argv = args;
	char** envp = argv + ret + 1;

	if((ret = spawn_child(ctx, argv, envp)) < 0)
		return ret;

	return 0;
}

static int cmd_start_one(CTX, CN, MSG)
{
	char* name = uc_get_str(msg, ATTR_ARG);

	if(!name)
		return -EINVAL;
	if(already_running(ctx, name))
		return -EALREADY;

	return common_spawn(ctx, msg);
}

static int cmd_spawn_new(CTX, CN, MSG)
{
	return common_spawn(ctx, msg);
}

static struct proc* find_proc(CTX, int xid)
{
	struct proc* pc = ctx->procs;
	struct proc* pe = pc + ctx->nprocs;

	for(; pc < pe; pc++)
		if(pc->xid == xid)
			return pc;

	return NULL;
}

static int signal_proc(CTX, MSG, int sig)
{
	struct proc* pc;
	int ret, pid, *xid;

	if(!(xid = uc_get_int(msg, ATTR_XID)))
		return -EINVAL;
	if(!(pc = find_proc(ctx, *xid)))
		return -ESRCH;
	if((pid = pc->pid) <= 0)
		return -ECHILD;
	if((ret = sys_kill(pid, sig)) < 0)
		return ret;

	return 0;
}

static int cmd_sigterm(CTX, CN, MSG)
{
	return signal_proc(ctx, msg, SIGTERM);
}

static int cmd_sigkill(CTX, CN, MSG)
{
	return signal_proc(ctx, msg, SIGKILL);
}

static int cmd_fetch_out(CTX, CN, MSG)
{
	struct proc* pc;
	int ret, *xid;
	void* buf;

	if(!(xid = uc_get_int(msg, ATTR_XID)))
		return -EINVAL;
	if(!(pc = find_proc(ctx, *xid)))
		return -ESRCH;
	if(!(buf = pc->buf))
		return -ENOENT;

	int size = RING_SIZE;
	int ptr = pc->ptr;
	int off = ptr % size;
	int est = 64 + (ptr >= size ? size : ptr);

	if((ret = ensure_iobuf(ctx, est)) < 0)
		return ret;

	(void)msg;
	struct ucbuf uc;

	uc_buf_set(&uc, ctx->iobuf, ctx->iolen);

	uc_put_hdr(&uc, 0);

	if(ptr <= size) {
		uc_put_bin(&uc, ATTR_RING, buf, ptr);
	} else {
		uc_put_bin(&uc, ATTR_RING, buf + off, size - off);
		uc_put_bin(&uc, ATTR_RING, buf, off);
	}

	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int cmd_flush_out(CTX, CN, MSG)
{
	struct proc* pc;
	int ret, *xid;

	if(!(xid = uc_get_int(msg, ATTR_XID)))
		return -EINVAL;
	if(!(pc = find_proc(ctx, *xid)))
		return -ESRCH;
	if((ret = flush_proc(ctx, pc)) < 0)
		return ret;

	return 0;
}

static const struct cmd {
	int cmd;
	int (*call)(CTX, CN, MSG);
} commands[] = {
	{ CMD_STATUS,    cmd_status    },
	{ CMD_SPAWN_NEW, cmd_spawn_new },
	{ CMD_START_ONE, cmd_start_one },
	{ CMD_SIGTERM,   cmd_sigterm   },
	{ CMD_SIGKILL,   cmd_sigkill   },
	{ CMD_FETCH_OUT, cmd_fetch_out },
	{ CMD_FLUSH_OUT, cmd_flush_out }
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

void close_conn(CTX, struct conn* cn)
{
	struct conn* conns = ctx->conns;

	if(cn->fd < 0)
		return; /* should never happen */

	sys_close(cn->fd);

	cn->fd = -1;

	ctx->nconns_active--;
	ctx->pollset = 0;

	int nconns = ctx->nconns;

	while(nconns > 0) {
		int i = nconns - 1;
		struct conn* ci = &conns[i];

		if(ci->fd >= 0)
			break;

		nconns--;
	}

	ctx->nconns = nconns;
}

void handle_conn(CTX, CN)
{
	int ret, fd = cn->fd;
	struct ucmsg* msg;

	if((ret = prep_recv_buffer(ctx)) < 0)
		goto err;

	void* iobuf = ctx->iobuf;
	int iolen = ctx->iolen;

	if((ret = uc_recv_whole(fd, iobuf, iolen)) < 0)
		goto err;
	if(!(msg = uc_msg(iobuf, ret)))
		goto err;
	if((ret = dispatch(ctx, cn, msg)) >= 0)
		return;
err:
	close_conn(ctx, cn);
}

static struct conn* grab_conn_slot(CTX)
{
	struct conn* conns = ctx->conns;
	struct conn* cn;
	int nconns = ctx->nconns;

	for(cn = conns; cn < conns + nconns; cn++)
		if(cn->fd < 0)
			goto out;

	if(nconns >= NCONNS)
		return NULL;

	ctx->nconns = nconns + 1;

	cn = &conns[nconns];
out:
	ctx->pollset = 0;
	ctx->nconns_active++;

	return cn;
}

void check_socket(CTX)
{
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn *cn;
	int flags = SOCK_NONBLOCK | SOCK_CLOEXEC;
	int sfd = ctx->ctlfd;
	int cfd;

	if((cfd = sys_accept4(sfd, &addr, &addr_len, flags)) >= 0)
		;
	else if(cfd == -EAGAIN)
		return;
	else
		quit("accept", NULL, cfd);

	if(!(cn = grab_conn_slot(ctx)))
		sys_close(cfd);
	else
		cn->fd = cfd;
}
