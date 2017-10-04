#include <sys/file.h>
#include <sys/fpath.h>

#include <string.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

int cmd_cd(CTX)
{
	char* dir;

	if(shift_str(ctx, &dir))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sys_chdir(dir), ctx, dir);
}

int cmd_exec(CTX)
{
	if(noneleft(ctx))
		return -1;

	char** argv = argsleft(ctx);

	return fchk(execvpe(*argv, argv, ctx->envp), ctx, *argv);
}

int cmd_exit(CTX)
{
	int code = 0;

	if(!numleft(ctx))
		;
	else if(shift_int(ctx, &code))
		return -1;

	_exit(code);
}

int cmd_invoke(CTX)
{
	int nargs = numleft(ctx);
	int norig = ctx->topargc - ctx->topargp;

	if(!nargs)
		return error(ctx, "too few arguments", NULL, 0);

	char** args = ctx->argv + ctx->argp;
	char** orig = ctx->topargv + ctx->topargp;

	char* argv[nargs + norig + 1];

	memcpy(argv, args, nargs*sizeof(char*));
	memcpy(argv + nargs, orig, norig*sizeof(char*));
	argv[nargs+norig] = NULL;

	return fchk(execvpe(*argv, argv, ctx->envp), ctx, *argv);
}

static int print(CTX, int fd)
{
	char* msg;

	if(shift_str(ctx, &msg))
		return -1;
	if(moreleft(ctx))
		return -1;

	int len = strlen(msg);
	msg[len] = '\n';
	sys_write(fd, msg, len+1);
	msg[len] = '\0';

	return 0;
}

int cmd_echo(CTX)
{
	return print(ctx, STDOUT);
}

int cmd_warn(CTX)
{
	return print(ctx, STDERR);
}

int cmd_die(CTX)
{
	cmd_warn(ctx);
	_exit(0xFF);
}

/* This assigns an executable to be exec()ed into in case of error.

   Current implementation is extremely crude, but so far there is
   exactly one use for this, invoking /sbin/system/reboot in pid 0
   scripts, so anything more would be an overkill. */

int cmd_onerror(CTX)
{
	char* arg;

	if(shift_str(ctx, &arg))
		return -1;
	if(moreleft(ctx))
		return -1;

	uint len = strlen(arg);

	if(len == 1 && arg[0] == '-') {
		memzero(ctx->trap, sizeof(ctx->trap));
		return 0;
	} else if(len + 1 > sizeof(ctx->trap)) {
		return error(ctx, "command too long", NULL, 0);
	}

	memcpy(ctx->trap, arg, len);

	return 0;
}
