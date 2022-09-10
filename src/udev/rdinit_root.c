#include <bits/magic.h>

#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/mount.h>
#include <sys/fpath.h>
#include <sys/statfs.h>
#include <sys/dents.h>

#include <printf.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "rdinit.h"

/* Initramfs cleanup is basically just `rm -fr /` so some extra care
   is taken here to make sure only the ramfs nodes are being removed.

   At the time this code runs, cwd is /mnt and / is the ramfs. */

static void delete_rec(CTX, int fd, char* dir);

static void make_path(char* path, int plen, char* dir, char* name)
{
	char* p = path;
	char* e = path + plen - 1;

	p = fmtstr(p, e, dir);
	p = fmtchar(p, e, '/');
	p = fmtstr(p, e, name);

	*p++ = '\0';
}

static void delete_ent(CTX, int at, char* dir, struct dirent* de)
{
	int fd, ret;
	struct stat st;
	char* name = de->name;

	int plen = strlen(dir) + strlen(name) + 5;
	char* path = alloca(plen);

	make_path(path, plen, dir, name);

	if(de->type != DT_DIR) {
		ret = sys_unlinkat(at, name, 0);
		goto err;
	}

	if((fd = sys_openat(at, name, O_DIRECTORY)) < 0) {
		ret = fd;
		goto err;
	}

	if((ret = sys_fstatat(fd, "", &st, AT_EMPTY_PATH)) < 0) {
		; /* do nothing */
	} else if(st.dev == ctx->olddev) {
		delete_rec(ctx, fd, path);
		ret = sys_unlinkat(at, name, AT_REMOVEDIR);
	} else if(st.dev == ctx->newdev) {
		/* do nothing */
	} else {
		warn("mounted:", path, 0);
		ret = sys_umount(path, MNT_DETACH);
	}

	if((fd = sys_close(fd)) < 0)
		fail("close", NULL, fd);
err:
	if(ret) warn(NULL, path, ret);
}

static void delete_rec(CTX, int fd, char* dir)
{
	char debuf[1024];
	int delen = sizeof(debuf);
	long rd;

	while((rd = sys_getdents(fd, debuf, delen)) > 0)
	{
		char* ptr = debuf;
		char* end = debuf + rd;

		while(ptr < end)
		{
			struct dirent* de = (struct dirent*) ptr;

			ptr += de->reclen;

			if(!de->reclen)
				break;
			if(dotddot(de->name))
				return;

			delete_ent(ctx, fd, dir, de);
		}
	};
}

void clear_initramfs(CTX)
{
	struct stat st;
	struct statfs sf;
	int fd, ret;

	if((ret = sys_stat(".", &st)) < 0)
		fail("stat", ".", ret);

	ctx->newdev = st.dev;

	if((fd = sys_open("/", O_DIRECTORY)) < 0)
		fail("open", "/", fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", "/", ret);

	ctx->olddev = st.dev;

	if(ctx->newdev == ctx->olddev)
		abort(ctx, "new root was not mounted", NULL);

	if((ret = sys_statfs("/", &sf)) < 0)
		fail("statfs", "/", ret);
	if(sf.type != RAMFS_MAGIC && sf.type != TMPFS_MAGIC)
		abort(ctx, "the root is not a ramfs", NULL);

	delete_rec(ctx, fd, "");

	if(sys_close(fd) < 0)
		fail("close", NULL, ret);
}
