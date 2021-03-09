#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/dents.h>

#include <util.h>

#include "ctool.h"

/* Commands for basic operations on the top-level tooldir.

   Like git, ctool is expected to run from the tooldir.
   Also like git, it uses a hidden .ctool stamp-file as
   an indicator that the directory is managed by ctool.
   This is mostly an extra precaution to avoid running
   rm-like subcommands in arbitrary directories. */

void check_workdir(CTX)
{
	int ret;
	struct stat st;
	char* name = ".ctool";

	if((ret = sys_stat(name, &st)) >= 0)
		;
	else if(ret == -ENOENT)
		fail("invalid workdir", NULL, 0);
	else
		fail(NULL, name, ret);

	if((st.mode & S_IFMT) != S_IFREG)
		fail("not a regular file:", name, 0);

	ctx->at = AT_FDCWD;
}

static void make_subdir(CTX, const char* name)
{
	int ret, mode = 0755;

	if((ret = sys_mkdir(name, mode)) < 0)
		fail(NULL, name, ret);
}

static void check_empty_dir(CTX)
{
	int fd, ret;
	char buf[512];

	if((fd = sys_open(".", O_DIRECTORY)) < 0)
		fail("cannot open current directory", NULL, fd);

	while((ret = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		char* ptr = buf;
		char* end = buf + ret;

		while(ptr < end)
		{
			struct dirent* de = (struct dirent*) ptr;
			ptr += de->reclen;

			if(!de->reclen)
				break;
			if(dotddot(de->name))
				continue;

			fail("non-empty directory", NULL, 0);
		}
	}
}

static void check_no_stamp(CTX)
{
	int ret;
	struct stat st;

	if((ret = sys_stat(".ctool", &st)) >= 0)
		fail("already initialized", NULL, 0);
	if(ret != -ENOENT)
		fail(NULL, ".ctool", ret);
}

static void create_stamp(CTX)
{
	int ret, mode = S_IFREG | 0000;
	char* name = ".ctool";

	if((ret = sys_mknod(name, mode, 0)) < 0)
		fail(NULL, name, ret);
}

void cmd_init(CTX)
{
	check_no_stamp(ctx);
	check_empty_dir(ctx);
	create_stamp(ctx);

	make_subdir(ctx, "bin");
	make_subdir(ctx, "lib");
	make_subdir(ctx, "inc");
	make_subdir(ctx, "pkg");
}

void cmd_repo(CTX)
{
	char* path;
	int ret;
	struct stat st;

	path = shift(ctx);

	no_more_arguments(ctx);

	if((ret = sys_stat(path, &st)) < 0)
		fail(NULL, path, ret);
	if((st.mode & S_IFMT) != S_IFDIR)
		fail(NULL, path, -ENOTDIR);

	check_workdir(ctx);

	if((ret = sys_unlink("rep")) >= 0)
		;
	else if(ret == -ENOENT)
		;
	else
		fail(NULL, "rep", ret);

	if((ret = sys_symlink(path, "rep")) < 0)
		fail(NULL, "rep", ret);
}

static void clear_bindir(CTX)
{
	uint size = 4096;
	void* buf = alloc_exact(ctx, size);
	int fd, ret;
	char* dir = "bin";

	if((fd = sys_open("bin", O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);

	while((ret = sys_getdents(fd, buf, size)) > 0) {
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

			if((ret = sys_unlinkat(fd, name, 0)) >= 0)
				continue;

			warn(NULL, name, ret);
			ctx->fail = 1;
		}
	} if(ret < 0) {
		fail("getdents", NULL, ret);
	}

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);

	if(ctx->fail) _exit(0xFF);
}

void cmd_rebin(CTX)
{
	no_more_arguments(ctx);

	check_workdir(ctx);

	clear_bindir(ctx);
}

void cmd_clear(CTX)
{
	no_more_arguments(ctx);

	cmd_rebin(ctx);
	cmd_reset(ctx);
}
