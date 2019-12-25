#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>

#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

static int wflags(CTX, char* str)
{
	char lead = *str;

	if(!lead)
		return O_WRONLY;
	if(str[1])
		fatal(ctx, "multiple options", NULL);
	if(lead == 'c')
		return O_WRONLY | O_CREAT | O_TRUNC;
	if(lead == 'x')
		return O_WRONLY | O_CREAT | O_EXCL;

	fatal(ctx, "unexpected options", NULL);
}

static void open_onto_fd(CTX, int tofd, char* name, int fl, int md)
{
	int fd;

	if((fd = sys_open3(name, fl, md)) < 0)
		error(ctx, "open", name, fd);
	if(fd == tofd)
		return;

	int ret = sys_dup2(fd, tofd);

	check(ctx, NULL, name, ret);

	sys_close(fd);
}

static void open_output(CTX, int tofd)
{
	int flags = O_WRONLY;
	char* opts;

	if((opts = dash_opts(ctx)))
		flags = wflags(ctx, opts);

	char* name = shift(ctx);

	no_more_arguments(ctx);

	open_onto_fd(ctx, tofd, name, flags, 0666);
}

void cmd_stdin(CTX)
{
	char* name = shift(ctx);

	no_more_arguments(ctx);

	open_onto_fd(ctx, STDIN, name, O_RDONLY, 0000);
}

static void save_stderr(CTX)
{
	int fd;

	if(ctx->errfd != STDERR)
		return;
	if((fd = sys_fcntl3(STDERR, F_DUPFD_CLOEXEC, 0)) < 0)
		return;

	ctx->errfd = fd;
}

void cmd_stdout(CTX)
{
	open_output(ctx, STDOUT);
}

void cmd_stderr(CTX)
{
	save_stderr(ctx);
	open_output(ctx, STDERR);
}

void cmd_stdtwo(CTX)
{
	open_output(ctx, STDOUT);
	save_stderr(ctx);

	int ret = sys_dup2(STDOUT, STDERR);

	check(ctx, "dup", NULL, ret);
}

void cmd_reopen(CTX)
{
	char* name = shift(ctx);

	no_more_arguments(ctx);

	open_onto_fd(ctx, STDIN,  name, O_RDONLY, 0000);
	open_onto_fd(ctx, STDOUT, name, O_WRONLY, 0000);

	int ret = sys_dup2(STDOUT, STDERR);

	check(ctx, "dup", NULL, ret);
}

void cmd_close(CTX)
{
	int fd;

	shift_int(ctx, &fd);
	no_more_arguments(ctx);

	int ret = sys_close(fd);

	check(ctx, "close", NULL, ret);
}

void cmd_write(CTX)
{
	int fd, ret;
	char* data = shift(ctx);
	char* name = shift(ctx);

	no_more_arguments(ctx);

	if((fd = sys_open(name, O_WRONLY)) < 0)
		error(ctx, NULL, name, fd);

	int len = strlen(data);
	data[len] = '\n';

	if((ret = sys_write(fd, data, len+1)) < 0)
		error(ctx, "write", name, ret);
	else if(ret < len)
		fatal(ctx, "incomplete write", NULL);

	data[len] = '\0';
}

void cmd_unlink(CTX)
{
	char* name = shift(ctx);

	no_more_arguments(ctx);

	int ret = sys_unlink(name);

	if(ret >= 0)
		return;
	if(ret == -ENOENT)
		return;

	error(ctx, "unlink", name, ret);
}

static void chown(CTX, char* owner, char* name)
{
	int uid, gid;
	char* sep = strcbrk(owner, ':');

	if(!*sep || !*(sep+1)) /* "user" or "user:" */
		gid = -1;
	else 
		gid = get_group_id(ctx, sep + 1);

	*sep = '\0';

	if(sep == owner) /* ":group" */
		uid = -1;
	else
		uid = get_user_id(ctx, owner);

	int ret = sys_chown(name, uid, gid);

	check(ctx, "chown", name, ret);
}

void cmd_mkdir(CTX)
{
	int mode = 0755;
	char *owner = NULL;
	int ret;

	char* name = shift(ctx);

	if(got_more_arguments(ctx))
		shift_oct(ctx, &mode);
	if(got_more_arguments(ctx))
		owner = shift(ctx);

	if((ret = sys_mkdir(name, mode)) >= 0)
		;
	else if(ret != -EEXIST)
		error(ctx, "mkdir", name, ret);

	if(owner) chown(ctx, owner, name);
}
