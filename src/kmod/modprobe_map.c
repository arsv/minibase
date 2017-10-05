#include <sys/file.h>
#include <sys/info.h>
#include <sys/mman.h>
#include <sys/module.h>
#include <sys/proc.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "modprobe.h"

/* The module must be loaded to process memory prior to init_module call.
   Uncompressed modules are mmaped whole. For compresses ones, the output
   of decompressor process is read into a growing chunk of memory. */

#define PAGE 4096
#define MAX_FILE_SIZE 20*1024*1024 /* 20MB */

static char* mmapanon(long size)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	char* ret = sys_mmap(NULL, size, prot, flags, -1, 0);

	if(mmap_error(ret))
		fail("mmap", NULL, (long)ret);

	return ret;
}

static char* mextend(char* buf, long size, long newsize)
{
	char* ret = sys_mremap(buf, size, newsize, MREMAP_MAYMOVE);

	if(mmap_error(ret))
		fail("mremap", NULL, (long)ret);

	return ret;
}

static int child(int fds[2], char* cmd, char* path, char** envp)
{
	char* argv[] = { cmd, path, NULL };

	xchk(sys_close(fds[0]), "close", NULL);
	xchk(sys_dup2(fds[1], STDOUT), "dup2", NULL);
	long ret = sys_execve(cmd, argv, envp);

	fail("cannot exec", cmd, ret);
	return 0xFF;
}

static void readall(struct mbuf* mb, int fd, char* cmd)
{
	const int unit = 4*PAGE;
	long len = unit;
	long ptr = 0;
	long rd;

	char* buf = mmapanon(unit);

	while((rd = sys_read(fd, buf + ptr, len - ptr)) > 0) {
		ptr += rd;
		if(ptr < len) continue;
		buf = mextend(buf, len, len + unit);
		len += unit;
	} if(rd < 0) {
		fail("read", cmd, rd);
	}

	mb->buf = buf;
	mb->len = ptr;
	mb->full = len;
}

void decompress(CTX, struct mbuf* mb, char* path, char* cmd)
{
	int fds[2];
	xchk(sys_pipe2(fds, 0), "pipe", NULL);

	int pid = xchk(sys_fork(), "fork", NULL);

	if(pid == 0)
		_exit(child(fds, cmd, path, ctx->envp));

	sys_close(fds[1]);

	readall(mb, fds[0], cmd);

	int status;
	xchk(sys_waitpid(pid, &status, 0), "wait", cmd);

	if(status) fail("non-zero exit code in", cmd, 0);
}

void mmap_whole(struct mbuf* mb, char* name, int mode)
{
	int fd;
	long ret;
	struct stat st;

	if(mode == NEWMAP)
		;
	else if(mb->tried)
		return;

	mb->tried = 1;

	if((fd = sys_open(name, O_RDONLY)) >= 0)
		;
	else if(mode != FAILOK)
		fail("open", name, fd);
	else return;

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	char* ptr = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if(mmap_error(ptr))
		fail("mmap", name, (long)ptr);

	if(st.size > MAX_FILE_SIZE)
		fail("file too large:", name, 0);

	mb->buf = ptr;
	mb->len = mb->full = st.size;
}

void unmap_buf(struct mbuf* mb)
{
	sys_munmap(mb->buf, mb->full);
	memzero(mb, sizeof(*mb));
}

static void prep_heap(CTX)
{
	void* brk;
	void* end;

	if(ctx->brk)
		return;

	brk = sys_brk(0);
	end = sys_brk(brk + PAGE);

	if(brk_error(brk, end))
		fail("cannot allocate memory", NULL, 0);

	ctx->brk = brk;
	ctx->lwm = brk;
	ctx->ptr = brk;
	ctx->end = end;
}

void* heap_alloc(CTX, int size)
{
	prep_heap(ctx);

	char* ptr = ctx->ptr;
	char* req = ptr + size;
	char* end = ctx->end;

	if(req < end)
		goto done;

	int aligned = size + (PAGE - size % PAGE) % PAGE;

	end = sys_brk(ptr + aligned);

	if(brk_error(ptr, end))
		fail("cannot allocate memory", NULL, 0);

	ctx->end = end;
done:
	ctx->ptr = req;

	return ptr;
}

void flush_heap(CTX)
{
	ctx->ptr = ctx->lwm;
}

char* heap_dupn(CTX, char* str, int len)
{
	char* buf = heap_alloc(ctx, len + 1);

	memcpy(buf, str, len);

	buf[len] = '\0';

	return buf;
}

char* heap_dupe(CTX, char* p, char* e)
{
	return heap_dupn(ctx, p, e - p);
}

char* heap_dup(CTX, char* str)
{
	return heap_dupn(ctx, str, strlen(str));
}
