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

#define MSG struct ucattr* msg
#define CN struct conn* cn __unused

#define MAX_COMMAND_BUF 8192
#define IOBUF_TIMER 30

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
	int ret, fd = cn->fd;

	if((ret = uc_send(fd, uc)) != -EAGAIN)
		return ret;
	if((ret = uc_wait_writable(fd)) < 0)
		return ret;

	return uc_send(fd, uc);
}

static int send_reply_iov(struct conn* cn, struct iovec* iov, int iovcnt)
{
	int ret, fd = cn->fd;

	if((ret = uc_send_iov(fd, iov, iovcnt)) != -EAGAIN)
		return ret;
	if((ret = uc_wait_writable(fd)) < 0)
		return ret;

	return uc_send_iov(fd, iov, iovcnt);
}

static int reply(struct conn* cn, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);

	return send_reply(cn, &uc);
}

static int any_procs_left(struct proc* pc, struct proc* pe)
{
	int xid;

	for(; pc < pe; pc++)
		if((xid = pc->xid))
			return xid;

	return 0;
}

static int reply_status(CTX, CN, int start, int nprocs)
{
	struct ucbuf uc;

	struct proc* procs = ctx->procs;
	struct proc* pc = procs + start;
	struct proc* pe = pc + nprocs;

	int maxrec = 16 + 5*sizeof(struct ucattr)
		+ 3*sizeof(int) + sizeof(pc->name);

	uc_buf_set(&uc, ctx->iobuf, ctx->iolen);
	uc_put_hdr(&uc, 0);

	for(; pc < pe; pc++) {
		struct ucattr* at;
		int xid = pc->xid;
		int pid = pc->pid;
		int ptr = pc->ptr;

		if(uc_space_left(&uc) < maxrec)
			break;
		if(!xid)
			continue;

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

	if(any_procs_left(pc, pe))
		uc_put_int(&uc, ATTR_NEXT, pc - procs);

	return send_reply(cn, &uc);
}

static int cmd_status(CTX, CN, MSG)
{
	int* next = uc_get_int(msg, ATTR_NEXT);
	int start = next ? *next : 0;
	int nprocs = ctx->nprocs;

	if(start < 0)
		return -EINVAL;
	if(start >= nprocs)
		return 0;

	return reply_status(ctx, cn, start, nprocs);
}

/* Both argv and envp are transmitted as inline arrays like this:

       noisy₀with₀some₀arguments₀

   To convert those into proper arrays for execve(), we index them, putting
   pointers to individual args into pre-allocated area delimited by ap and ae:

       [0] = "noisy"     <-- ap
       [1] = "with"
       [2] = "some"
       [3] = "arguments" 
       [4] = ...         <-- return
       [5] = ...
       [6] = ...         <-- ae

   Note strs should be terminated, but there are no guarantees on that. */

static char** index_strings(char** ap, char** ae, struct ucattr* strs)
{
	char* p = uc_payload(strs);
	char* e = p + uc_paylen(strs);

	while(ap < ae) {
		char* q = p;

		while(q < e && *q) q++; /* advance to \0 */

		if(q >= e) break; /* no \0 terminator found */

		*ap++ = p;

		p = q + 1;
	}

	return ap;
}

static int prep_argv_envp(MSG, char** ptrs, int n)
{
	char** argp = ptrs;
	char** arge = ptrs + n;
	int argc;

	struct ucattr* argv = uc_get(msg, ATTR_ARGV);
	struct ucattr* envp = uc_get(msg, ATTR_ENVP);

	argp = index_strings(argp, arge, argv);

	if(argp >= arge)
		return -E2BIG;
	if(argp == ptrs)
		return -EINVAL;

	argc = argp - ptrs;
	*(argp++) = NULL;

	argp = index_strings(argp, arge, envp);

	if(argp >= arge)
		return -E2BIG;

	*(argp++) = NULL;

	return argc;
}

static int cmd_spawn(CTX, CN, MSG)
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

static int reply_ring(CTX, CN, void* ring, int ptr)
{
	struct ucbuf uc;
	char buf[128];
	struct iovec iov[3];
	int iovcnt, ret;

	if(ptr <= RING_SIZE) {
		iov[1].base = ring;
		iov[1].len = ptr;

		iovcnt = 2;
	} else {
		int size = RING_SIZE;
		int off = ptr % size;

		iov[1].base = ring + off;
		iov[1].len = size - off;
		iov[2].base = ring;
		iov[2].len = off;

		iovcnt = 3;
	}

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, 0);

	if((ret = uc_iov_hdr(&iov[0], &uc)) < 0)
		return ret;

	return send_reply_iov(cn, iov, iovcnt);
}

static int cmd_fetch(CTX, CN, MSG)
{
	struct proc* pc;
	int *xid;
	void* buf;

	if(!(xid = uc_get_int(msg, ATTR_XID)))
		return -EINVAL;
	if(!(pc = find_proc(ctx, *xid)))
		return -ESRCH;
	if(!(buf = pc->buf))
		return -ENOENT;

	int ptr = pc->ptr;

	return reply_ring(ctx, cn, buf, ptr);
}

static int cmd_flush(CTX, CN, MSG)
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
	{ CMD_SPAWN,     cmd_spawn     },
	{ CMD_SIGTERM,   cmd_sigterm   },
	{ CMD_SIGKILL,   cmd_sigkill   },
	{ CMD_FETCH,     cmd_fetch     },
	{ CMD_FLUSH,     cmd_flush     }
};

static int dispatch(CTX, CN, MSG)
{
	const struct cmd* cd;
	int cmd = uc_repcode(msg);
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
	struct ucattr* msg;

	if((ret = prep_recv_buffer(ctx)) < 0)
		goto err;

	void* iobuf = ctx->iobuf;
	int iolen = ctx->iolen;

	if((ret = uc_recv(fd, iobuf, iolen)) < 0)
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
		fail("accept", NULL, cfd);

	if(!(cn = grab_conn_slot(ctx)))
		sys_close(cfd);
	else
		cn->fd = cfd;
}

void setup_control(CTX)
{
	int fd, ret;
	int flags = SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = uc_listen(fd, CONTROL, 5)) < 0)
		fail("ucbind", CONTROL, ret);

	ctx->ctlfd = fd;
}
