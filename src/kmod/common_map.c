#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <string.h>
#include <format.h>
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
   calling code to make sure the same exact file is being mmaped.

   Support for external decompressors should probably be removed at
   some point, leaving .ko and .ko.lz the only options, but for now
   it makes testing this easier as host system modules can be used
   right away. */

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

/* Trim extra space in case the decompressed content ended up
   a lot shorter than the allocted buffer */

static int trim_extra(struct mbuf* mb)
{
	int ret;

	long ptr = mb->len;
	long len = mb->full;
	long newlen = pagealign(ptr);

	if(!newlen)
		newlen = PAGE;
	if(newlen >= len)
		return 0;

	void* oldbuf = mb->buf;
	char* newbuf = sys_mremap(oldbuf, len, newlen, MREMAP_MAYMOVE);

	if((ret = mmap_error(newbuf))) {
		warn("mremap", NULL, ret);
		return ret;
	}

	mb->len = newlen;
	mb->buf = newbuf;

	return 0;
}

static int readall(struct mbuf* mb, int fd, int compsize, char* cmd)
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

	mb->buf = buf;
	mb->len = ptr;
	mb->full = len;

	if((ret = trim_extra(mb)) < 0)
		goto err;

	return 0;
err:
	munmap_temp(buf, len);

	return ret;
}

/* We have module.ko.gz and we need to know if we can unpack it, that is,
   whether we have an unpacker script in place.

     script = "/etc/pac/gz"
     path = "/lib/modules/kernel/something/module.ko.gz"
     ext = "gz"

   Pretty much any invocation with multiple modules (which is most of them,
   even a simple modprobe with dependencies) will use the same compression
   for all modules in the batch. It is extremely unusual to have some .ko.gz
   and some .ko.bz2 or something in the same tree.

   So with that in mind, we cache the last result and skip the probe next
   time the same extension is used. */

static int check_decomp_available(struct upac* pc, char* script, char* path, char* ext)
{
	int ret;
	int extlen = strlen(ext);
	int maxlen = sizeof(pc->last) - 2;

	if(extlen > maxlen) {
		warn("suffix ignored:", ext, 0);
		return -ENAMETOOLONG;
	}

	char* last = pc->last + 1;
	char* lret = pc->last;

	if(!memcmp(last, ext, extlen) && !last[extlen]) {
		int failed = *lret;

		if(!failed)
			return 0;

		warn("skipping", path, 0);
		return -ENOENT;
	}

	memcpy(last, ext, extlen);
	last[extlen] = '\0';

	if((ret = sys_access(script, X_OK)) < 0) {
		*lret = 0xFF;
		warn("compression not supported:", ext, 0);
		warn("skipping", path, 0);
		return ret;
	}

	*lret = 0x00;

	return 0;
}

/* Make sure module.ko.gz exists first.
   It if doesn't, there's no point in spawning the unpacker script. */

static int check_file_usable(char* path)
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

static int decompress(struct mbuf* mb, char** args, char** envp)
{
	int ret, size, fds[2], pid, status;
	char* cmd = args[0];
	char* path = args[1];

	if((size = check_file_usable(path)) < 0)
		return size;

	if((ret = sys_pipe2(fds, 0)) < 0)
		fail("pipe", NULL, ret);
	if((pid = sys_fork()) < 0)
		fail("fork", NULL, ret);

	if(pid == 0)
		child(fds, args, envp);

	if((ret = sys_close(fds[1])) < 0)
		fail("close", "pipe", ret);

	int res = readall(mb, fds[0], size, cmd);

	if((ret = sys_close(fds[0])) < 0)
		fail("close", "pipe", ret);

	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		fail("wait", NULL, pid);

	if(status && (res >= 0)) { /* pipe command failed */
		warn("non-zero exit in", cmd, 0);
		munmap_buf(mb);
		return -1;
	}

	return res;
}

static int mmap_compressed(struct upac* pc, struct mbuf* mb, char* path, char* ext)
{
	char* dir = pc->sdir;

	if(!dir) { /* no support for compressed modules */
		warn("compressed module:", path, 0);
		return -EINVAL;
	}

	int dlen = strlen(dir);
	int xlen = strlen(ext);

	int len = dlen + xlen + 3;
	char* script = alloca(len);
	char* p = script;
	char* e = script + len - 1;

	p = fmtstr(p, e, dir);
	p = fmtchar(p, e, '/');
	p = fmtstr(p, e, ext);

	*p++ = '\0';

	char* argv[] = { script, path, NULL };
	char** envp = pc->envp;

	if(check_decomp_available(pc, script, path, ext))
		return -ENOENT;

	return decompress(mb, argv, envp);
}

static char* locate_backwards(char* p, char* e, char c)
{
	while(e > p) {
		e--;
		if(*e == c)
			return e;
	}
	return NULL;
}

static char* suffix_after_ko(char* path)
{
	char* base = basename(path);
	int blen = strlen(base);
	char* end = base + blen;

	char* p1 = locate_backwards(base, end, '.');

	if(!p1) /* "file-with-no-dots" */
		return NULL;
	if(!strcmp(p1 + 1, "ko")) /* file-name.ko */
		return end;
	if(end - p1 > 5)
		return NULL; /* very long extension */

	char* p2 = locate_backwards(base, p1, '.');

	if(!p2) /* file-name.gz */
		return NULL;
	if(p1 - p2 != 3 || memcmp(p2, ".ko", 3)) /* file-name.so.gz */
		return NULL;

	/* file-name.ko.gz or something like that */

	return p1 + 1;
}

int load_module(struct upac* pc, struct mbuf* mb, char* path)
{
	char* ext;

	memzero(mb, sizeof(*mb));

	if(!(ext = suffix_after_ko(path))) {
		warn("unexpected module extension:", path, 0);
		return -EINVAL;
	} else if(!*ext) { /* plain foo.ko */
		return mmap_whole(mb, path, REQ);
	} else if(!strcmp(ext, "lz")) {
		return map_lunzip(mb, path);
	}

	return mmap_compressed(pc, mb, path, ext);
}
