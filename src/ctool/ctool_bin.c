#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/dents.h>

#include <util.h>

#include "ctool.h"
#include "ctool_bin.h"

static void make_subdir(CTX, const char* name)
{
	int ret, mode = 0755;

	if((ret = sys_mkdir(name, mode)) >= 0)
		return;
	if(ret == -EEXIST)
		return;

	fail(NULL, name, ret);
}

static void make_top_dirs(CTX)
{
	make_subdir(ctx, "bin");
	make_subdir(ctx, "lib");
	make_subdir(ctx, "inc");
	make_subdir(ctx, "pkg");
}

static int check_stamp(CTX)
{
	char* name = ".ctool";
	struct stat st;
	int ret;

	if((ret = sys_lstat(name, &st)) >= 0)
		return 1;
	if(ret != -ENOENT)
		fail(NULL, name, ret);

	return 0;
}

void check_workdir(CTX)
{
	char* name = ".ctool";
	struct stat st;
	int ret;

	if((ret = sys_lstat(name, &st)) >= 0)
		return;
	if((st.mode & S_IFMT) != S_IFREG)
		fail("not a regular file:", name, 0);

	fail(NULL, name, ret);
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

static void open_stamp_file(CCT, int flags)
{
	char* name = ".ctool";
	int mode = 0644;
	int fd;

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail(NULL, name, fd);

	cct->fd = fd;
}

static void create_stamp_file(CCT)
{
	open_stamp_file(cct, O_WRONLY | O_CREAT | O_EXCL);
}

static void append_stamp_file(CCT)
{
	open_stamp_file(cct, O_WRONLY | O_APPEND);
}

static void reopen_stamp_file(CCT)
{
	open_stamp_file(cct, O_WRONLY | O_TRUNC);
}

static void write_stamp_file(CCT)
{
	void* buf = cct->wrbuf;
	uint size = cct->wrptr;
	int ret, fd = cct->fd;

	if(!size)
		fail("empty tool spec", NULL, 0);

	if((ret = sys_write(fd, buf, size)) < 0)
		fail("write", NULL, ret);

	cct->wrptr = 0;
}

void cmd_use(CTX)
{
	struct subcontext context, *cct = &context;

	int stamped = check_stamp(ctx);

	common_bin_init(ctx, cct);

	run_statements(cct, MD_DRY);

	if(!stamped)
		create_stamp_file(cct);
	else
		append_stamp_file(cct);

	run_statements(cct, MD_LIST);

	write_stamp_file(cct);

	if(!stamped)
		make_top_dirs(ctx);

	run_statements(cct, MD_REAL);
}

void cmd_rebin(CTX)
{
	struct subcontext context, *cct = &context;

	remove_bindir_files(ctx);

	common_bin_init(ctx, cct);

	run_statements(cct, MD_DRY);

	reopen_stamp_file(cct);

	run_statements(cct, MD_LIST);

	write_stamp_file(cct);

	run_statements(cct, MD_REAL);
}
