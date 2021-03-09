#include <sys/mman.h>
#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/proc.h>
#include <sys/dents.h>
#include <sys/prctl.h>

#include <config.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "ctool.h"

/* When ctool gets invokes as `ctool add foo`, we need to figure out what
   exactly to install as `foo`. This is somewhat non-trivial since we need
   to support compressed packages, so `foo` could refer to rep/foo.pac, or
   rep/foo.pac.gz, or rep/foo.pac.lz or whatever else.

   In order to read say foo.pac.gz, ctool spawns `/etc/mpac/gz foo.pac.gz`
   and reads its output. Like in other parts of the project, this is a
   de-coupling mechanism to avoid hard-coded dependency on gz/lzip/xz
   or searching them in $PATH.

   We also allow passing explicit paths to packages,

       ctool path/to/foo.pac
       ctool path/to/foo.pac.gz

   so there's some more code for picking decoders based on the file name. */

static char* copy_raw(CTX, char* str, int len)
{
	char* buf = alloc_align(ctx, len + 1);

	memcpy(buf, str, len);

	buf[len] = '\0';

	return buf;
}

static void alloc_transfer_buf(CTX)
{
	uint size = 1<<20;
	uint prot = PROT_READ | PROT_WRITE;
	uint flags = MAP_PRIVATE | MAP_ANONYMOUS;

	void* buf = sys_mmap(NULL, size, prot, flags, -1, 0);
	int ret;

	if(ctx->databuf && ctx->datasize != size)
		fail(NULL, NULL, -EFAULT);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	ctx->databuf = buf;
	ctx->datasize = size;
}

static void spawn_pipe(CTX, char* dec, char* path)
{
	int ret, pid, fds[2];

	alloc_transfer_buf(ctx);

	if((ret = sys_pipe(fds)) < 0)
		fail("pipe", NULL, ret);

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, 0);

	if(pid == 0) {
		char* args[] = { dec, path, NULL };

		if((ret = sys_prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0)) < 0)
			fail("prctl", NULL, ret);
		if((ret = sys_dup2(fds[1], 1)) < 0)
			fail("dup2", NULL, ret);
		if((ret = sys_close(fds[0])) < 0)
			fail("close", NULL, ret);
		if((ret = sys_close(fds[1])) < 0)
			fail("close", NULL, ret);

		ret = sys_execve(*args, args, ctx->envp);

		fail("execve", *args, ret);
	}

	if((ret = sys_close(fds[1])) < 0)
		fail("close", NULL, ret);

	ctx->pacfd = fds[0];
}

void prep_filedb_len(CTX, char* name, int nlen)
{
	int size = nlen + 16;

	char* path = alloc_align(ctx, size);
	char* p = path;
	char* e = path + size - 1;

	p = fmtstr(p, e, "pkg/");
	p = fmtstrn(p, e, name, nlen);
	p = fmtstr(p, e, ".list");

	*p++ = '\0';

	ctx->fdbpath = path;
}

static void load_uncompressed(CTX, char* path)
{
	int fd;

	if((fd = sys_open(path, O_RDONLY)) < 0)
		fail(NULL, path, fd);

	ctx->pacfd = fd;
	ctx->pacpath = path;
}

static void load_compressed(CTX, char* path, char* suff)
{
	FMTBUF(q, a, dec, 100);
	q = fmtstr(q, a, HERE "/etc/mpac/");
	q = fmtstr(q, a, suff);
	FMTEND(q, a);

	if(q >= a)
		fail(NULL, NULL, -ENAMETOOLONG);

	spawn_pipe(ctx, dec, path);
}

static char* skip_extension(char* p, char* e)
{
	while(p < e)
		if(*(--e) == '.')
			break;

	return e;
}

static int match_between(char* p, char* e, char* str)
{
	int len = strlen(str);

	if(e - p != len)
		return 0;
	if(memcmp(p, str, len))
		return 0;

	return -1;
}

static void locate_by_path(CTX, char* path)
{
	char* base = basename(path);
	int blen = strlen(base);
	char* bend = base + blen;

	char* last = skip_extension(base, bend);
	char* prev = skip_extension(base, last);

	if(match_between(last, bend, ".pac")) {
		prep_filedb_len(ctx, base, last - base);
		load_uncompressed(ctx, path);
		return;
	}

	if(match_between(prev, last, ".pac")) {
		prep_filedb_len(ctx, base, prev - base);
		load_compressed(ctx, path, last + 1);
		return;
	}
}

static int try_uncompressed(CTX, char* name)
{
	int fd;

	FMTBUF(p, e, buf, 200);
	p = fmtstr(p, e, "rep/");
	p = fmtstr(p, e, name);
	p = fmtstr(p, e, ".pac");
	FMTEND(p, e);

	if(p >= e)
		fail(NULL, NULL, -ENAMETOOLONG);

	if((fd = sys_open(buf, O_RDONLY)) >= 0)
		;
	else if(fd == -ENOENT)
		goto out;
	else
		fail(NULL, buf, fd);

	ctx->pacfd = fd;
	ctx->pacpath = copy_raw(ctx, buf, p - buf + 1);
out:
	return fd;
}

static void check_decoder(CTX, char* stem, char* dec)
{
	int slen = strlen(stem);
	int plen = slen + 32;

	if(ctx->pacpath) {
		if(!ctx->fail) {
			warn("multiple matches", NULL, 0);
			warn(NULL, ctx->pacpath, 0);
			ctx->fail = 1;
		}
		heap_reset(ctx, ctx->pacpath);
		ctx->pacpath = NULL;
	}

	char* path = alloc_align(ctx, plen);

	char* p = path;
	char* e = path + plen - 1;

	p = fmtstr(p, e, "rep/");
	p = fmtstr(p, e, stem);
	p = fmtstr(p, e, ".pac.");
	p = fmtstr(p, e, dec);

	*p++ = '\0';

	if(ctx->fail) {
		warn(NULL, path, 0);
		heap_reset(ctx, path);
		return;
	}

	if(sys_access(path, R_OK) < 0) {
		heap_reset(ctx, path);
		return;
	}

	ctx->pacpath = path;
}

/* In case foo.pac is available, we check possible decoders and try
   rep/foo.pac.$dec for each possible dec value, as opposed to scanning
   rep/ and looking for files starting with "foo.pac". Since /etc/mpac
   is host-controlled (and ./rep to some extent isn't), we can reasonably
   assume that the files there are "nice", i.e. no excessively long names,
   less entries overall and so on. */

static void scan_dec_directory(CTX, char* stem)
{
	char buf[1024];
	char* dir = HERE "/etc/mpac";
	int fd, ret;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		return;

	while((ret = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + ret;

		while(ptr < end) {
			struct dirent* de = ptr;
			int reclen = de->reclen;
			char* name = de->name;

			if(!reclen)
				break;

			ptr += reclen;

			if(dotddot(name))
				continue;
			if(sys_faccessat(fd, name, X_OK, 0) < 0)
				continue;

			check_decoder(ctx, stem, name);
		}
	} if(ret < 0) {
		fail("getdents", NULL, ret);
	}

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);
	if(ctx->fail)
		_exit(0xFF);
}

static void locate_by_name(CTX, char* name)
{
	prep_filedb_len(ctx, name, strlen(name));

	if(try_uncompressed(ctx, name) >= 0)
		return;

	scan_dec_directory(ctx, name);

	char* path = ctx->pacpath;

	if(!path) return; /* no matches */

	char* base = basename(path);
	char* bend = base + strlen(base);
	char* last = skip_extension(base, bend);
	char* prev = skip_extension(base, last);

	if(!match_between(prev, last, ".pac"))
		fail("bad matched name", NULL, 0);

	load_compressed(ctx, path, last + 1);
}

void locate_package(CTX, char* arg)
{
	if(looks_like_path(arg))
		locate_by_path(ctx, arg);
	else
		locate_by_name(ctx, arg);

	if(ctx->pacfd < 0)
		fail("cannot locate this package", NULL, 0);

	load_index(ctx);
}
