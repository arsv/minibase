#include <sys/file.h>
#include <sys/fpath.h>

#include <string.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

void cmd_cd(CTX)
{
	char* dir = shift(ctx);

	no_more_arguments(ctx);

	int ret = sys_chdir(dir);

	check(ctx, NULL, dir, ret);
}

void cmd_exit(CTX)
{
	int code;

	if(got_more_arguments(ctx))
		shift_int(ctx, &code);
	else
		code = 0;

	no_more_arguments(ctx);

	_exit(code);
}

static void print(CTX, int fd)
{
	char* msg = shift(ctx);

	no_more_arguments(ctx);

	int len = strlen(msg);

	msg[len] = '\n';
	int ret = writeall(fd, msg, len+1);
	msg[len] = '\0';

	check(ctx, "write", NULL, ret);
}

void cmd_echo(CTX)
{
	print(ctx, STDOUT);
}

void cmd_warn(CTX)
{
	print(ctx, STDERR);
}

void cmd_die(CTX)
{
	cmd_warn(ctx);
	exit_fail();
}
