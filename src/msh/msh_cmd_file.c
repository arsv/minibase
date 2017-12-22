#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>

#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

static int wflags(CTX, char* str, int* flags)
{
	switch(str[0]) {
		case '\0': *flags |= O_CREAT | O_TRUNC; break;
		case 'a':  *flags |= O_CREAT | O_APPEND; break;
		case 'x': break;
		default: return error(ctx, "open", "unknown flags", 0);
	}

	return 0;
}

static int open_onto_fd(CTX, int tofd, char* name, int fl, int md)
{
	int fd, ret;

	if((fd = sys_open3(name, fl, md)) < 0)
		return error(ctx, "open", name, fd);
	if(fd == tofd)
		return 0;

	ret = fchk(sys_dup2(fd, tofd), ctx, name);
	sys_close(fd);

	return ret;
}

static int open_output(CTX, int tofd)
{
	char* name;
	int flags = O_WRONLY;

	if(noneleft(ctx))
		return -1;

	char* flagstr = dasharg(ctx) ? shift(ctx)+1 : "";

	if(wflags(ctx, flagstr, &flags))
		return -1;
	if(shift_str(ctx, &name))
		return -1;
	if(moreleft(ctx))
		return -1;

	return open_onto_fd(ctx, tofd, name, flags, 0666);
}

int cmd_stdin(CTX)
{
	char* name;

	if(noneleft(ctx))
		return -1;
	if(shift_str(ctx, &name))
		return -1;
	if(moreleft(ctx))
		return -1;

	return open_onto_fd(ctx, STDIN, name, O_RDONLY, 0000);
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

int cmd_stdout(CTX)
{
	return open_output(ctx, STDOUT);
}

int cmd_stderr(CTX)
{
	save_stderr(ctx);
	return open_output(ctx, STDERR);
}

int cmd_stdtwo(CTX)
{
	int ret;

	if((ret = open_output(ctx, STDOUT)) < 0)
		return ret;

	save_stderr(ctx);
	return fchk(sys_dup2(STDOUT, STDERR), ctx, "dup2");
}

int cmd_dupfd(CTX)
{
	int oldfd, newfd;

	if(shift_int(ctx, &oldfd))
		return -1;
	if(shift_int(ctx, &newfd))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sys_dup2(oldfd, newfd), ctx, NULL);
}

int cmd_reopen(CTX)
{
	char* name;
	int ret = 0;

	if(shift_str(ctx, &name))
		return -1;
	if(moreleft(ctx))
		return -1;

	ret |= open_onto_fd(ctx, STDIN,  name, O_RDONLY, 0000);
	ret |= open_onto_fd(ctx, STDOUT, name, O_WRONLY, 0000);
	sys_dup2(STDOUT, STDERR);

	return ret;
}

int cmd_close(CTX)
{
	int fd;

	if(shift_int(ctx, &fd))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sys_close(fd), ctx, NULL);
}

int cmd_write(CTX)
{
	int fd, ret;
	char* data;
	char* name;

	if(shift_str(ctx, &data))
		return -1;
	if(shift_str(ctx, &name))
		return -1;
	if(moreleft(ctx))
		return -1;

	if((fd = sys_open(name, O_WRONLY)) < 0)
		return error(ctx, "open", name, 0);

	int len = strlen(data);
	data[len] = '\n';

	if((ret = sys_write(fd, data, len+1)) < 0)
		return error(ctx, "write", name, ret);
	else if(ret < len)
		return error(ctx, "incomplete write", NULL, 0);

	data[len] = '\0';

	return 0;
}

int cmd_unlink(CTX)
{
	char* name;
	int ret;

	if(noneleft(ctx))
		return -1;

	while((name = shift(ctx)))
		if((ret = sys_unlink(name)) >= 0)
			continue;
		else if(ret == -ENOENT)
			continue;
		else return error(ctx, *ctx->argv, name, ret);

	return 0;
}

int cmd_mkdir(CTX)
{
	int mode = 0755;
	char *name, *owner = NULL;
	int ret, uid, gid;

	if(shift_str(ctx, &name))
		return -1;
	if(numleft(ctx) && shift_oct(ctx, &mode))
		return -1;
	if(numleft(ctx) && shift_str(ctx, &owner))
		return -1;
	if(moreleft(ctx))
		return -1;

	if((ret = sys_mkdir(name, mode)) >= 0)
		;
	else if(ret == -EEXIST)
		return 0;
	else
		return error(ctx, "mkdir", name, ret);

	if(!owner)
		return 0;
	if(get_owner_ids(ctx, owner, &uid, &gid))
		return -1;
	if((ret = sys_chown(name, uid, gid)) < 0)
		return error(ctx, "chown", name, ret);

	return 0;
}
