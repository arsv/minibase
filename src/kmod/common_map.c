#include <sys/file.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <string.h>
#include <util.h>

#include "common.h"

/* The idea here is to take a file name and return a mmap'ed buffer
   with the content of the file. For uncompressed modules, that means
   just mmaping the file. Compressed .gz and .xz modules get unpacked
   into a mmaped memory area. */ 

#define MAX_MAP_SIZE 0x7FFFFFFF

int mmap_whole(CTX, struct mbuf* mb, char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0) {
		error(ctx, NULL, name, fd);
		return fd;
	}

	if((ret = sys_fstat(fd, &st)) < 0) {
		error(ctx, "stat", name, ret);
		goto out;
	}
	if(st.size > MAX_MAP_SIZE) {
		ret = -E2BIG;
		error(ctx, NULL, name, ret);
		goto out;
	}

	int prot = PROT_READ;
	int flags = MAP_PRIVATE;
	void* buf;

	buf = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if((ret = mmap_error(buf)) < 0)
		goto out;

	mb->buf = buf;
	mb->len = st.size;
	mb->full = pagealign(st.size);
out:
	sys_close(fd);

	return ret;
}

void munmap_buf(struct mbuf* mb)
{
	sys_munmap(mb->buf, mb->full);
	memzero(mb, sizeof(*mb));
}

/* Decompression */

static noreturn void child(int fds[2], char** argv, char** envp)
{
	int ret;

	if((ret = sys_close(fds[0])) < 0)
		fail("close", NULL, ret);
	if((ret = sys_dup2(fds[1], STDOUT)) < 0)
		fail("dup2", NULL, ret);

	ret = execvpe(*argv, argv, envp);

	fail("cannot exec", *argv, ret);
}

static int readall(CTX, struct mbuf* mb, int fd, struct stat* st, char* cmd)
{
	const int unit = 4*PAGE;
	uint ptr = 0, len = pagealign(8*st->size);
	long rd, ret;

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	char* buf = sys_mmap(NULL, len, prot, flags, -1, 0);

	if((ret = mmap_error(buf)))
		return error(ctx, "mmap", NULL, ret);

	while((rd = sys_read(fd, buf + ptr, len - ptr)) > 0) {
		ptr += rd;

		if(ptr < len)
			continue;

		long newlen = len + unit;
		char* newbuf = sys_mremap(buf, len, newlen, MREMAP_MAYMOVE);

		if((ret = mmap_error(newbuf)))
			return error(ctx, "mremap", NULL, ret);

		len = newlen;
		buf = newbuf;
	} if(rd < 0) {
		fail("read", cmd, rd);
	}

	if(pagealign(ptr) < len) {
		long newlen = pagealign(ptr);
		char* newbuf = sys_mremap(buf, len, newlen, MREMAP_MAYMOVE);

		if((ret = mmap_error(newbuf)))
			return error(ctx, "mremap", NULL, ret);

		len = newlen;
		buf = newbuf;
	}

	mb->buf = buf;
	mb->len = ptr;
	mb->full = len;

	return 0;
}

int decompress(CTX, struct mbuf* mb, char* path, char** args)
{
	int pid, ret, fds[2], status;
	char* cmd = *args;
	struct stat st;

	if((ret = sys_stat(path, &st)) < 0)
		return error(ctx, NULL, path, ret);
	if(st.size > MAX_MAP_SIZE/16)
		return error(ctx, NULL, path, -E2BIG);

	if((ret = sys_pipe2(fds, 0)) < 0)
		return error(ctx, "pipe", NULL, ret);

	if((pid = sys_fork()) < 0)
		return error(ctx, "fork", NULL, ret);
	if(pid == 0)
		child(fds, args, environ(ctx));

	sys_close(fds[1]);

	(void)readall(ctx, mb, fds[0], &st, cmd);

	sys_close(fds[0]);

	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		return error(ctx, "wait", cmd, pid);
	if(status)
		return error(ctx, "non-zero exit in", cmd, 0);

	return 0;
}

static int check_suffix(char* name, int nlen, char* suffix)
{
	int slen = strlen(suffix);

	if(nlen < slen)
		return 0;
	if(strncmp(name + nlen - slen, suffix, slen))
		return 0;

	return 1;
}

static int map_zcat(CTX, struct mbuf* mb, char* path)
{
	char* args[] = { "gzip", "-dc", path, NULL };
	return decompress(ctx, mb, path, args);
}

static int map_xzcat(CTX, struct mbuf* mb, char* path)
{
	char* args[] = { "xz", "-dc", path, NULL };
	return decompress(ctx, mb, path, args);
}

static int map_zstd(CTX, struct mbuf* mb, char* path)
{
	char* args[] = { "zstd", "-dcf", path, NULL };
	return decompress(ctx, mb, path, args);
}

int load_module(CTX, struct mbuf* mb, char* path)
{
	char* base = basename(path);
	int blen = strlen(base);

	if(check_suffix(base, blen, ".ko"))
		return mmap_whole(ctx, mb, path);
	if(check_suffix(base, blen, ".ko.lz"))
		return map_lunzip(ctx, mb, path);
	if(check_suffix(base, blen, ".ko.gz"))
		return map_zcat(ctx, mb, path);
	if(check_suffix(base, blen, ".ko.xz"))
		return map_xzcat(ctx, mb, path);
	if(check_suffix(base, blen, ".ko.zst"))
		return map_zstd(ctx, mb, path);

	error(ctx, "unexpected module extension:", base, 0);
	return -EINVAL;
}
