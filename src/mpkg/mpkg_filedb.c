#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/mman.h>

#include <config.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "mpkg.h"

/* During package installation, after processing the .pac index, but before
   unpacking the content, we store the list of files about to be unpacked
   in a pre-defined location (below), so that we'd be able to remove them later.

   The format used for this is similar to .pac, except there are no
   sizes for leaf nodes, and no content of course.

   Un-installation subsequently means loading that file and unlinking whatever
   is listed there. Since the format is much simpler, we do not bother indexing
   it and just re-parse it where appropriate. */

#define DATADIR BASE_VAR "/mpkg"

struct pkgcontext {
	void* buf;
	uint size;

	byte* ptr;
	byte* end;
};

#define PCT struct pkgcontext* pct

/* Attempts to install a package that has already been installed should be
   reported very early, and as clearly as possible. To do that, we stat
   the .pkg file before attempting running filesystem conflict checks.

   Trying to open it here is a bad idea, as failure anywhere else from this
   point on would leave an empty .pkg file in place. This is however just a
   bit of user-friendliness, we'll still have a chance to fail while doing
   open(O_CREAT | O_EXCL) later, ensuring overall correctness. */

static void stat_pkg_file(CTX)
{
	int at = ctx->rootfd;
	char* name = pkg_name(ctx);
	int flags = AT_SYMLINK_NOFOLLOW;
	struct stat st;
	int ret;

	name = root_adjust(name);

	if((ret = sys_fstatat(at, name, &st, flags)) >= 0)
		failz(ctx, NULL, name, -EEXIST);

	if(ret == -ENOTDIR)
		return;
	if(ret == -ENOENT)
		return;

	failz(ctx, NULL, name, ret);
}

static void open_pkg_file(CTX)
{
	int at = ctx->rootfd;
	char* name = pkg_name(ctx);
	int flags = O_WRONLY | O_CREAT | O_EXCL;
	int mode = 0644;
	int fd;

	name = root_adjust(name);

	if((fd = sys_openat4(at, name, flags, mode)) < 0)
		failz(ctx, NULL, name, fd);

	ctx->pkgfd = fd;
}

static void prep_pkg_name(CTX)
{
	char* repo = reg_repo(ctx);
	char* name = reg_name(ctx);

	int len = strlen(DATADIR) + 2;

	len += strlen(name) + 10;

	if(repo) len += strlen(repo) + 2;

	char* buf = heap_alloc(ctx, len);

	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, DATADIR);
	p = fmtstr(p, e, "/");

	if(repo) {
		p = fmtstr(p, e, repo);
		p = fmtstr(p, e, "/");
	}

	p = fmtstr(p, e, name);
	p = fmtstr(p, e, ".pkg");

	*p++ = '\0';

	ctx->pkg = buf;
}

static void prep_repo_dir(CTX)
{
	int at = ctx->rootfd;
	char* name = pkg_name(ctx);
	char* base = basename(name);
	int ret;

	if(!ctx->repo) return;

	name = root_adjust(name);

	if(base == name) return; /* very unlikely */

	base--; *base = '\0';

	if((ret = sys_mkdirat(at, name, 0755)) < 0 && ret != -EEXIST)
		failz(ctx, NULL, name, ret);

	*base = '/';
}

static void load_pkg_file(CTX)
{
	int at = ctx->rootfd;
	char* name = pkg_name(ctx);
	struct stat st;
	int fd, ret;

	name = root_adjust(name);

	if((fd = sys_openat(at, name, O_RDONLY)) < 0)
		failz(ctx, NULL, name, fd);
	if((ret = sys_flock(fd, LOCK_EX)) < 0)
		failz(ctx, "lock", name, ret);
	if((ret = sys_fstat(fd, &st)) < 0)
		failz(ctx, "stat", name, ret);
	if(st.size > 0xFFFFFFFF)
		failz(ctx, NULL, name, -E2BIG);

	uint size = st.size;
	int proto = PROT_READ;
	int flags = MAP_PRIVATE;

	void* buf = sys_mmap(NULL, size, proto, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	ctx->head = buf;
	ctx->hoff = 0;
	ctx->hlen = size;
	ctx->pkgfd = fd;
}

void load_filedb(CTX)
{
	prep_pkg_name(ctx);
	load_pkg_file(ctx);
}

/* At the end of removal procedure, once the package files are gone,
   unlinke filedb, marking the package as "uninstalled".

   In case of repo:name, also try to remove the corresponding repo
   directory from under /var/mpkg. That should only succeed if the
   package was the last one installed from this repo though,
   so ENOTEMPTY is a perfectly normal outcome. */

void unlink_filedb(CTX)
{
	char* name = pkg_name(ctx);
	int ret, at = ctx->rootfd;

	name = root_adjust(name);

	if((ret = sys_unlinkat(at, name, 0)) < 0)
		failz(ctx, NULL, name, ret);

	if(!ctx->repo)
		return;

	char* base = basename(name);

	if(base == name)
		return;

	base--; *base = '\0';

	if((ret = sys_unlinkat(at, name, AT_REMOVEDIR)) >= 0)
		;
	else if(ret != -ENOTEMPTY)
		failz(ctx, NULL, name, ret);

	*base = '/';
}

void check_filedb(CTX)
{
	prep_pkg_name(ctx);
	stat_pkg_file(ctx);
}

static void append_node(CTX, PCT, struct node* nd)
{
	char* name = nd->name;
	int bits = nd->bits;
	int nlen = strlen(name);

	if(!(bits & TAG_DIR))
		bits &= ~TAG_SIZE;

	int need = nlen + 2;

	byte* ptr = pct->ptr;
	byte* end = pct->end;
	byte* new = ptr + need;

	if(new > end)
		failz(ctx, NULL, ctx->pkg, -ENOMEM);

	*ptr = bits;
	memcpy(ptr + 1, name, nlen + 1);

	pct->ptr = new;
}

static void format_entries(CTX, PCT)
{
	struct node* nd = ctx->index;
	struct node* ne = nd + ctx->nodes;

	for(; nd < ne; nd++) {
		int bits = nd->bits;

		if(!(bits & BIT_NEED))
			continue;

		append_node(ctx, pct, nd);
	}
}

static void write_entries(CTX, PCT)
{
	void* buf = pct->buf;
	void* ptr = pct->ptr;
	uint len = ptr - buf;

	int fd = ctx->pkgfd;
	int ret;

	if((ret = sys_write(fd, buf, len)) < 0)
		failz(ctx, "write", ctx->pkg, ret);
	if(ret != len)
		failz(ctx, "write", ctx->pkg, -EINTR);

	sys_close(ctx->pkgfd);

	ctx->pkgfd = -1;
}

static void init_pkg_context(CTX, PCT)
{
	uint size = 4 + ctx->hlen;
	char* pref = ctx->pref;
	int plen = pref ? strlen(pref) : 0;

	if(pref) size += plen + 2;

	void* buf = heap_alloc(ctx, size);

	memzero(pct, sizeof(*pct));

	pct->size = size;

	pct->buf = buf;
	pct->size = size;
	pct->end = buf + size;

	memcpy(buf, "MPKG", 4);
	buf += 4;

	if(pref) {
		byte* p = buf;

		*p++ = 0xFF;
		memcpy(p, pref, plen + 1);
		buf += 2 + plen;
	}

	pct->ptr = buf;
}

void write_filedb(CTX)
{
	struct pkgcontext context, *pct = &context;

	void* ptr = ctx->ptr;

	prep_repo_dir(ctx);

	open_pkg_file(ctx);

	init_pkg_context(ctx, pct);

	format_entries(ctx, pct);

	write_entries(ctx, pct);

	ctx->ptr = ptr;
}
