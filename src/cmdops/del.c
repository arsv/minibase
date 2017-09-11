#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/dents.h>

#include <string.h>
#include <printf.h>
#include <format.h>
#include <errtag.h>
#include <util.h>

#define OPTS "nfxdZ"
#define OPT_n (1<<0)    /* non-recursively */
#define OPT_f (1<<1)    /* force */
#define OPT_x (1<<2)    /* cross fs boundaries */
#define OPT_d (1<<3)    /* rmdir */
#define OPT_Z (1<<4)    /* no-keep-root */

#define DEBUFSIZE 2048

ERRTAG("del");
ERRLIST(NEACCES NEBUSY NEFAULT NEIO NEISDIR NELOOP NENAMETOOLONG NENOENT
	NENOMEM NENOTDIR NEPERM NEROFS NEINVAL NENOTEMPTY);

struct top {
	int opts;
	int root;

	uint64_t rdev; /* root */
	uint64_t rino;

	uint64_t sdev; /* starting dir */
};

struct rfn {
	int at;
	char* dir;
	char* name;
};

#define CTX struct top* ctx
#define FN struct rfn* fn

#define AT(dd) dd->at, dd->name

static void delete(CTX, FN, int asdir);

static int pathlen(FN)
{
	int len = strlen(fn->name);

	if(fn->dir && fn->name[0] != '/')
		len += strlen(fn->dir) + 1;

	return len + 1;
}

static void makepath(char* buf, int len, FN)
{
	char* p = buf;
	char* e = buf + len - 1;

	if(fn->dir && fn->name[0] != '/') {
		p = fmtstr(p, e, fn->dir);
		p = fmtstr(p, e, "/");
	};

	p = fmtstr(p, e, fn->name);
	*p = '\0';
}

static void failat(CTX, FN, int err)
{
	int opts = ctx->opts;

	char path[pathlen(fn)];
	makepath(path, sizeof(path), fn);

	warn(NULL, path, err);

	if(opts & OPT_f) return;

	_exit(-1);
}

static void stat_root(CTX)
{
	int ret;
	struct stat st;
	int at = AT_FDCWD;
	int flags = AT_SYMLINK_NOFOLLOW;

	if((ret = sys_fstatat(at, "/", &st, flags) < 0))
		fail("cannot stat", "/", ret);

	ctx->rdev = st.dev;
	ctx->rino = st.ino;
	ctx->root = 1;
}

static int check_xdev(CTX, FN, int fd)
{
	int opts = ctx->opts;
	int flags = AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH;
	int at = fn->at;
	struct stat st;
	int ret;

	if((opts & OPT_Z) && (opts & OPT_x) && at != AT_FDCWD)
		return 0; /* no point in checking the dir */

	if(!ctx->root && !(opts & OPT_Z))
		stat_root(ctx);

	if((ret = sys_fstatat(fd, "", &st, flags)) < 0) {
		failat(ctx, fn, ret);
		return -1; /* better safe than sorry */
	}

	if(opts & OPT_Z)
		;
	else if(st.dev == ctx->rdev && st.ino == ctx->rino)
		fail("refusing to delete root", NULL, 0);

	if(at == AT_FDCWD) /* top-level invocation */
		ctx->sdev = st.dev;
	else if(opts & OPT_x)
		return 0; /* can cross dev boundaries */
	else if(ctx->sdev != st.dev)
		return -1; /* no crossing */

	return 0;
}

static void enter(CTX, FN)
{
	int len = DEBUFSIZE;
	char buf[len];
	int fd, rd;

	if((fd = sys_openat(AT(fn), O_DIRECTORY)) < 0)
		return failat(ctx, fn, fd);

	char path[pathlen(fn)];
	makepath(path, sizeof(path), fn);
	struct rfn next = { fd, path, NULL };

	if(check_xdev(ctx, fn, fd))
		goto out;

	while((rd = sys_getdents(fd, buf, len)) > 0) {
		char* p = buf;
		char* e = buf + rd;

		while(p < e) {
			struct dirent* de = (struct dirent*) p;
			p += de->reclen;

			if(!de->reclen)
				break;
			if(dotddot(de->name))
				continue;

			next.name = de->name;

			delete(ctx, &next, de->type);
		}
	};
out:
	sys_close(fd);
};

static void delete(CTX, FN, int type)
{
	int ret;
	int opts = ctx->opts;

	if(opts & OPT_d)
		goto rmd;
	if(type == DT_DIR)
		goto dir;

	if((ret = sys_unlinkat(AT(fn), 0)) >= 0)
		return;
	if(ret != -EISDIR)
		goto fail;
dir:
	if(opts & OPT_n)
		goto rmd;

	enter(ctx, fn);
rmd:
	if((ret = sys_unlinkat(AT(fn), AT_REMOVEDIR)) >= 0)
		return;
fail:
	failat(ctx, fn, ret);
}

int main(int argc, char** argv)
{
	int opts = 0, i = 1;
	struct top ctx;

	memzero(&ctx, sizeof(ctx));

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i >= argc)
		fail("too few arguments", NULL, 0);

	ctx.opts = opts;

	while(i < argc) {
		char* name = argv[i++];
		struct rfn fn = { AT_FDCWD, NULL, name };

		delete(&ctx, &fn, DT_UNKNOWN);
	}

	return 0;
}
