#include <sys/file.h>
#include <sys/mman.h>
#include <sys/fpath.h>

#include <output.h>
#include <string.h>
#include <util.h>

#include "mpkg.h"

#define AT_SKIP -1

/* Removal procedure is a bit more involved than just doing
   unlink(line) for each line in the list file.

       /usr/bin/foo
       /usr/bin/bar
       ...

   Given the list above, the code here does

       open("/usr/bin", O_DIRECTORY) = N
       unlinkat(N, "foo")
       unlinkat(N, "bar")

   This is done mostly to keep symmetry with the deploy code,
   which naturally uses at-calls, and also because it makes
   removing directories a bit easiers. */

static void backtrack_path(CTX, int lvl)
{
	int depth = ctx->depth;
	int ret;

	for(int i = depth - 1; i >= lvl; i--) {
		int fd = ctx->at;

		if(fd == AT_SKIP)
			;
		else if((ret = sys_close(fd)) < 0)
			warn("close", NULL, ret);

		int at = ctx->pfds[i];
		char* name = ctx->path[i];

		if((at == AT_SKIP))
			;
		else if((ret = sys_unlinkat(at, name, AT_REMOVEDIR)) >= 0)
			;
		else if((ret != -ENOENT) && (ret != -ENOTEMPTY))
			warnx(ctx, NULL, name, ret);

		ctx->at = at;
		ctx->ptr = name;
	}

	ctx->depth = lvl;
}

/* Given path = [ "usr", "lib" ] and p = "usr/share/foo",
   close "lib" to leave [ "usr" ] in path and return "share/foo" */

static char* skip_open_dirs(CTX, char* p, char* e)
{
	int depth = ctx->depth;
	int lvl = 0;

	while((p < e) && (lvl < depth)) {
		char* ds = p;
		char* de = strecbrk(p, e, '/');

		if(de >= e)
			break;

		int dl = de - ds;
		char* pi = ctx->path[lvl];
		int pl = strnlen(pi, dl + 1);

		if(pl != dl || memcmp(pi, ds, dl))
			break;

		p = de + 1;
		lvl++;
	}

	backtrack_path(ctx, lvl);

	return p;
}

/* Given path = [ "usr" ] and p = "lib/foo.so", open "lib", push it into
   path yielding [ "usr", "lib" ] and return the bare basename, "foo.so". */

static char* enter_new_dirs(CTX, char* p, char* e)
{
	int ret;

	while(p < e) {
		char* nptr = p;
		char* nend = strecbrk(p, e, '/');

		if(nend >= e)
			break;

		p = nend + 1;

		char* name = copy_string(ctx, nptr, nend);
		/*           ^ ptr reset in backtrack_path! */

		int at = ctx->at;
		int lvl = ctx->depth;

		if(lvl >= MAXDEPTH)
			fail("max depth exceeded", NULL, 0);

		if(at == AT_SKIP) {
			ret = at;
		} else if((ret = sys_openat(at, name, O_DIRECTORY)) < 0) {
			if(ret != -ENOENT)
				fail("open", name, ret);
			ret = AT_SKIP;
		}

		ctx->pfds[lvl] = at;
		ctx->at = ret;
		ctx->path[lvl] = name;
		ctx->depth = lvl + 1;
	}

	return p;
}

static char* enter_skip_dir(CTX, char* p, char* e)
{
	p = skip_open_dirs(ctx, p, e);

	p = enter_new_dirs(ctx, p, e);

	return p;
}

static void unlink_tail(CTX, char* p, char* e)
{
	int ret, at = ctx->at;
	void* old = ctx->ptr;
	int len = e - p;

	if(at == AT_SKIP)
		return;

	char* tmp = alloc_align(ctx, len + 1);
	memcpy(tmp, p, len);
	tmp[len] = '\0';

	if((ret = sys_unlinkat(at, tmp, 0)) >= 0)
		;
	else if(ret != -ENOENT)
		warnx(ctx, NULL, tmp, ret);

	ctx->ptr = old;
}

static void unlink_files(CTX)
{
	char* p = ctx->head + ctx->hoff;
	char* e = ctx->head + ctx->hlen;

	while(p < e) {
		char* ls = p;
		char* le = strecbrk(p, e, '\n');

		p = le + 1;

		uint plen = ctx->prelen;

		if(le - ls < plen)
			fail("invalid prefix", NULL, 0);

		ls += plen;

		char* bn = enter_skip_dir(ctx, ls, le);

		unlink_tail(ctx, bn, le);
	}

	if(ctx->fail) fail("unable to continue", NULL, 0);

	backtrack_path(ctx, 0);
}

/* Prefix, if present, is the first line and it ends with a slash.

      /opt/
      /opt/foo/bar.so
      /opt/foo/blah.bin

   No regular paths in the list should ever end with a slash, mpkg
   does not track directories explicitly.

   Missing prefix means the paths were /-relative during deployment. */

static void check_prefix(CTX)
{
	char* p = ctx->head;
	char* e = p + ctx->hlen;

	if(p >= e)
		return;

	char* ls = p;
	char* le = strecbrk(p, e, '\n');

	if(ls >= le)
		return;

	char* slash = le - 1;
	char* next = le + 1;

	if(*slash != '/')
		return;

	if(ls >= le)
		return;

	ctx->prefix = copy_string(ctx, ls, slash);
	ctx->prelen = le - ls;
	ctx->hoff = (next - ls);
}

/* The `list` just dumps the file list to stdout.

   This made a lot more sense with the binary file lists, but even with
   the move to plaintext list, it allows the user to specify package name
   instead of trying to figure out the full path to the list. */

static void dump_filedb(CTX)
{
	void* buf = ctx->head + ctx->hoff;
	void* end = ctx->head + ctx->hlen;
	uint len = end - buf;
	int ret, fd = STDOUT;

	if((ret = writeall(fd, buf, len)) < 0)
		fail(NULL, NULL, ret);
}

/* At the end of removal procedure, once the package files are gone,
   unlink filedb, marking the package as "uninstalled".

   In case of repo:name, also try to remove the corresponding repo
   directory from under /var/mpkg. That should only succeed if the
   package was the last one installed from this repo though,
   so ENOTEMPTY is a perfectly normal outcome. */

void unlink_filedb(CTX)
{
	char* name = ctx->lstname;
	int ret;

	if((ret = sys_unlink(name)) < 0)
		fail(NULL, name, ret);

	if(!ctx->group)
		return;

	char* base = basename(name);

	if(base == name)
		return;

	base--; *base = '\0';

	if((ret = sys_rmdir(name)) >= 0)
		;
	else if(ret != -ENOTEMPTY)
		fail(NULL, name, ret);

	*base = '/';
}

static void load_filedb(CTX)
{
	char* name = ctx->lstname;
	struct stat st;
	int fd, ret;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_flock(fd, LOCK_EX)) < 0)
		fail("lock", name, ret);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);
	if(st.size > 0xFFFFFFFF)
		fail(NULL, name, -E2BIG);
	if(!st.size)
		return;

	uint size = st.size;
	int proto = PROT_READ;
	int flags = MAP_PRIVATE;

	void* buf = sys_mmap(NULL, size, proto, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	ctx->head = buf;
	ctx->hlen = size;
	ctx->lstfd = fd;
}

static void setup_prefix(CTX)
{
	int fd, flags = O_DIRECTORY | O_PATH;
	char* path = ctx->prefix;

	if(!path)
		ctx->prefix = path = "/";

	if((fd = sys_open(path, flags)) < 0)
		fail(NULL, path, fd);

	ctx->at = fd;
}

void cmd_list(CTX)
{
	take_package_arg(ctx);
	no_more_arguments(ctx);

	prep_lstname(ctx);
	load_filedb(ctx);

	check_prefix(ctx);
	dump_filedb(ctx);
}

void cmd_remove(CTX)
{
	take_package_arg(ctx);
	no_more_arguments(ctx);

	prep_lstname(ctx);

	load_filedb(ctx);

	check_prefix(ctx);
	setup_prefix(ctx);

	unlink_files(ctx);
	unlink_filedb(ctx);
}
