#include <sys/file.h>
#include <sys/proc.h>
#include <sys/creds.h>
#include <sys/fprop.h>
#include <sys/signal.h>
#include <sys/mman.h>
#include <sys/iovec.h>

#include <string.h>
#include <format.h>
#include <sigset.h>
#include <util.h>

#include "common.h"
#include "apphub.h"

#define MAX_RUNNING_PROCS 4096

static void* prep_pipe_buf(struct proc* pc)
{
	void* buf = pc->buf;

	if(buf) return buf;

	int size = RING_SIZE;
	int proto = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	buf = sys_mmap(NULL, size, proto, flags, -1, 0);

	if(mmap_error(buf))
		return NULL;

	pc->buf = buf;
	pc->ptr = 0;

	return buf;
}

void handle_pipe(CTX, struct proc* pc)
{
	void* buf;

	if(!(buf = prep_pipe_buf(pc)))
		return close_pipe(ctx, pc);

	int size = RING_SIZE;
	int ptr = pc->ptr;
	int off = ptr % size;
	int ret, fd = pc->fd;

	struct iovec iov[2] = {
		{ .base = buf + off, .len = size - off },
		{ .base = buf, .len = off }
	};
	int iocnt = off ? 2 : 1;

	if((ret = sys_readv(fd, iov, iocnt)) < 0)
		return;

	ptr += ret;

	if(ptr > size)
		ptr = size + (ptr % size);

	pc->ptr = ptr;
}

void close_pipe(CTX, struct proc* pc)
{
	int fd = pc->fd;

	if(fd <= 0) return;

	del_poll_fd(ctx, fd);

	(void)sys_close(fd);

	pc->fd = -1;
}

static int unmap_pipe(CTX, struct proc* pc)
{
	void* buf = pc->buf;
	int size = RING_SIZE;
	int ret;

	if(!buf) return 0;

	if((ret = sys_munmap(buf, size)) < 0)
		return ret;

	pc->buf = NULL;
	pc->ptr = 0;

	return 0;
}

static int xid_is_in_use(CTX, int val)
{
	struct proc* pc = ctx->procs;
	struct proc* pe = pc + ctx->nprocs;

	for(; pc < pe; pc++) {
		int xid = pc->xid;

		if(!xid)
			continue;
		if(xid < 0)
			xid = -xid;
		if(xid == val)
			return xid;
	}

	return 0;
}

static int pick_unused_xid(CTX)
{
	int xid = ctx->lastxid + 1;
	int aprocs = ctx->nprocs_nonempty;
	int i, tries = aprocs + 1;

	if(xid > 100 && xid > 2*aprocs)
		xid = 1;

	for(i = 0; i < tries; i++)
		if(!xid_is_in_use(ctx, xid))
			break;
	if(i >= tries)
		return -EAGAIN;

	ctx->lastxid = xid;

	return xid;
}

static inline int empty(struct proc* pc)
{
	return !(pc->xid);
}

static void update_proc_counts(CTX)
{
	int i, nprocs = ctx->nprocs;
	struct proc* procs = ctx->procs;
	int limit = 0, nonempty = 0, running = 0;

	for(i = 0; i < nprocs; i++) {
		struct proc* pc = &procs[i];

		if(!pc->xid)
			continue;

		nonempty++;
		limit = i + 1;

		if(pc->pid <= 0)
			continue;

		running++;
	}

	ctx->nprocs = limit;
	ctx->nprocs_nonempty = nonempty;
	ctx->nprocs_running = running;

	maybe_trim_heap(ctx);
}

static void wipe_proc(struct proc* pc)
{
	memzero(pc, sizeof(*pc));
}

static void drop_proc(CTX, struct proc* pc)
{
	wipe_proc(pc);
}

static struct proc* grab_proc(CTX)
{
	int nprocs = ctx->nprocs;
	struct proc* pc = ctx->procs;
	struct proc* pe = pc + nprocs;

	for(; pc < pe; pc++)
		if(empty(pc))
			goto out;

	if(extend_heap(ctx, pc + 1) < 0)
		return NULL;

	ctx->nprocs = nprocs + 1;
out:
	memzero(pc, sizeof(*pc));

	return pc;
}

static void wipe_stale_entries(CTX, struct proc* px)
{
	struct proc* pc = ctx->procs;
	struct proc* pe = pc + ctx->nprocs;

	for(; pc < pe; pc++) {
		if(pc == px)
			continue;
		if(!pc->xid)
			continue;
		if(pc->pid > 0)
			continue;
		if(memcmp(px->name, pc->name, sizeof(pc->name)))
			continue;

		wipe_proc(pc);
	}

	update_proc_counts(ctx);
}

static void mark_dead(CTX, struct proc* pc, int status)
{
	pc->pid = (0xFFFF0000 | (status & 0xFFFF));
}

void check_children(CTX)
{
	int pid, status;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
		struct proc* pc = ctx->procs;
		struct proc* pe = pc + ctx->nprocs;

		for(; pc < pe; pc++)
			if(pc->pid == pid)
				break;
		if(pc >= pe)
			return;

		if(!status && !pc->buf)
			drop_proc(ctx, pc);
		else
			mark_dead(ctx, pc, status);

	} if(pid < 0 && pid != -ECHILD) {
		fail("waitpid", NULL, pid);
	}

	update_proc_counts(ctx);
}

int flush_proc(CTX, struct proc* pc)
{
	int ret;

	if((ret = unmap_pipe(ctx, pc)) < 0)
		return ret;

	if(pc->pid <= 0) {
		drop_proc(ctx, pc);
		update_proc_counts(ctx);
	}

	return 0;
}

static int prep_path(char* path, int len, char* name)
{
	char* p = path;
	char* e = path + len;

	p = fmtstr(p, e, CONFDIR);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);

	if(p >= e) return -ENAMETOOLONG;

	*p = '\0';

	return 0;
}

static int child(int pipe[2], char* path, char** argv, char** envp)
{
	struct sigset empty;
	int ret;

	sigemptyset(&empty);

	if((ret = sys_sigprocmask(SIG_SETMASK, &empty, NULL)) < 0)
		fail("sigprocmask", NULL, ret);
	if((ret = sys_close(pipe[0])) < 0)
		fail("close", NULL, ret);

	int fd = pipe[1];

	if((ret = sys_dup2(fd, 0)) < 0)
		fail("dup2", "stdin", ret);
	if((ret = sys_dup2(fd, 1)) < 0)
		fail("dup2", "stdout", ret);
	if((ret = sys_dup2(fd, 2)) < 0)
		fail("dup2", "stderr", ret);

	if((ret = sys_setsid()) < 0)
		fail("setsid", NULL, ret);

	ret = sys_execve(path, argv, envp);

	fail("execve", path, ret);
}

static int spawn_proc(struct proc* pc, char* path, char** argv, char** envp)
{
	int pid, ret, pipe[2];

	if((ret = sys_pipe2(pipe, O_NONBLOCK)) < 0)
		return ret;

	if((pid = sys_fork()) < 0)
		return ret;

	if(pid == 0)
		_exit(child(pipe, path, argv, envp));

	sys_close(pipe[1]);

	pc->fd = pipe[0];
	pc->pid = pid;

	return 0;
}

static int validate_name(char* name)
{
	byte* p = (byte*)name;
	byte c;

	while((c = *p++))
		if(c < 0x20 || c == '/')
			return -EINVAL;

	return 0;
}

int spawn_child(CTX, char** argv, char** envp)
{
	struct proc* pc;
	char path[100];
	char* name = argv[0];
	int nlen = strlen(name);
	int xid, ret;

	if(nlen > ssizeof(pc->name) - 1)
		return -ENAMETOOLONG;
	if(validate_name(name))
		return -ENOENT;
	if(ctx->nprocs_running > MAX_RUNNING_PROCS)
		return -EMFILE;
	if((xid = pick_unused_xid(ctx)) <= 0)
		return -EAGAIN;

	if((ret = prep_path(path, sizeof(path), name)) < 0)
		return ret;

	if((ret = sys_access(path, X_OK)) < 0)
		return ret;
	if(!(pc = grab_proc(ctx)))
		return -ENOMEM;

	memzero(pc, sizeof(*pc));
	memcpy(pc->name, name, nlen);

	pc->xid = xid;

	if((ret = spawn_proc(pc, path, argv, envp)) < 0) {
		wipe_proc(pc);
		return ret;
	}

	add_pipe_fd(ctx, pc->fd, pc);
	wipe_stale_entries(ctx, pc);

	return xid;
}
