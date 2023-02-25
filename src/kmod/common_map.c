#include <sys/file.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <string.h>
#include <util.h>

#include "common.h"

/* The idea here is to take a file name and return a mmap'ed buffer
   with the content of the file. For uncompressed modules, that means
   just mmaping the file. Compressed .gz and .xz modules get unpacked
   into a mmaped memory area.

   There's also some support for on-demand mapping as some of the code
   needs it for things like modules.dep and modules.alias.
   If mbuf has been mmaped already, we just return the results of the
   first attempt (either success or the error code). It is up to the
   calling code to make sure the same exact file is being mmaped. */

#define MAX_MAP_SIZE 0x7FFFFFFF

static const char empty_area[4];

static int open_for_reading(char* name, int optional)
{
	int fd;

	if((fd = sys_open(name, O_RDONLY)) < 0) {
		if(!optional)
			fail(NULL, name, fd);
		else if(fd != -ENOENT)
			warn(NULL, name, fd);
	}

	return fd;
}

static int stat_for_size(int fd, char* name)
{
	struct stat st;
	int ret;

	if((ret = sys_fstat(fd, &st)) < 0) {
		fail("stat", name, ret);
		return ret;
	}
	if(st.size > MAX_MAP_SIZE) {
		fail(NULL, name, -E2BIG);
		return ret;
	}

	return st.size;
}

int mmap_whole(struct mbuf* mb, char* name, int optional)
{
	int ret;

	if(mb->buf) /* already mapped */
		return 0;
	if((ret = mb->full) < 0)
		return ret; /* prev error */
	else if(ret)
		fail("positive error code while mmaping", NULL, 0);

	if((ret = open_for_reading(name, optional)) < 0)
		goto outr;

	int fd = ret;

	if((ret = stat_for_size(fd, name)) < 0)
		goto outc;

	int size = ret;
	int prot = PROT_READ;
	int flags = MAP_PRIVATE;
	void* buf;

	if(size == 0)
		buf = (void*)empty_area;
	else
		buf = sys_mmap(NULL, size, prot, flags, fd, 0);

	if((ret = mmap_error(buf)))
		goto outc;

	mb->buf = buf;
	mb->len = size;
	mb->full = size;
outc:
	sys_close(fd);
outr:
	if(ret) mb->full = ret;

	return ret;
}

void munmap_buf(struct mbuf* mb)
{
	if(mb->full)
		sys_munmap(mb->buf, mb->full);

	memzero(mb, sizeof(*mb));
}

/* Decompression for compressed modules.

   Unlike with config files, module mbufs are never re-used, so they get
   silently memzero'd here. A second temporary mbuf is allocated to hold
   the compressed contents of the file. */

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

static void munmap_temp(void* buf, uint len)
{
	int ret;

	if((ret = sys_munmap(buf, len)) < 0)
		warn("munmap", NULL, ret);
}

static int readall(CTX, struct mbuf* mb, int fd, int compsize, char* cmd)
{
	const int unit = 4*PAGE;
	long ptr = 0, len = pagealign(8*compsize + 1);
	long rd, ret;

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	char* buf = sys_mmap(NULL, len, prot, flags, -1, 0);

	if((ret = mmap_error(buf))) {
		warn("mmap", NULL, ret);
		return ret;
	}

	while((rd = sys_read(fd, buf + ptr, len - ptr)) > 0) {
		ptr += rd;

		if(ptr < len)
			continue;

		long newlen = len + unit;

		if(newlen > MAX_MAP_SIZE) {
			warn("compressed module too larger", NULL, 0);
			ret = -E2BIG;
			goto err;
		}

		char* newbuf = sys_mremap(buf, len, newlen, MREMAP_MAYMOVE);

		if((ret = mmap_error(newbuf))) {
			warn("mremap", NULL, ret);
			goto err;
		}

		len = newlen;
		buf = newbuf;
	} if(rd < 0) {
		warn("read", cmd, rd);
		ret = rd;
		goto err;
	}

	/* Trim extra space in case the decompressed content ended up
	   a lot shorter than the allocted buffer */
	if(pagealign(ptr) < len) {
		long newlen = pagealign(ptr);
		char* newbuf = sys_mremap(buf, len, newlen, MREMAP_MAYMOVE);

		if((ret = mmap_error(newbuf))) {
			warn("mremap", NULL, ret);
			goto err;
		}

		len = newlen;
		buf = newbuf;
	}

	mb->buf = buf;
	mb->len = ptr;
	mb->full = len;

	return 0;
err:
	munmap_temp(buf, len);

	return ret;
}

static int check_file_usable(CTX, char* path)
{
	int ret;
	struct stat st;

	if((ret = sys_stat(path, &st)) < 0) {
		warn(NULL, path, ret);
		return ret;
	}

	if(st.size > MAX_MAP_SIZE/16) {
		ret = -E2BIG;
		warn(NULL, path, ret);
		return ret;
	}

	return st.size;
}

int decompress(CTX, struct mbuf* mb, char* path, char** args)
{
	int ret, size, fds[2], pid, status;
	char* cmd = *args;

	if((size = check_file_usable(ctx, path)) < 0)
		return size;

	if((ret = sys_pipe2(fds, 0)) < 0)
		fail("pipe", NULL, ret);
	if((pid = sys_fork()) < 0)
		fail("fork", NULL, ret);

	if(pid == 0)
		child(fds, args, environ(ctx));

	if((ret = sys_close(fds[1])) < 0)
		fail("close", "pipe", ret);

	int res = readall(ctx, mb, fds[0], size, cmd);

	if((ret = sys_close(fds[0])) < 0)
		fail("close", "pipe", ret);

	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		fail("wait", cmd, pid);

	if(status && (res >= 0)) { /* pipe command failed */
		warn("non-zero exit in", cmd, 0);
		munmap_buf(mb);
		return -1;
	}

	return res;
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

	memzero(mb, sizeof(*mb));

	if(check_suffix(base, blen, ".ko"))
		return mmap_whole(mb, path, REQ);
	if(check_suffix(base, blen, ".ko.lz"))
		return map_lunzip(mb, path);
	if(check_suffix(base, blen, ".ko.gz"))
		return map_zcat(ctx, mb, path);
	if(check_suffix(base, blen, ".ko.xz"))
		return map_xzcat(ctx, mb, path);
	if(check_suffix(base, blen, ".ko.zst"))
		return map_zstd(ctx, mb, path);

	warn("unexpected module extension:", base, 0);
	return -EINVAL;
}
