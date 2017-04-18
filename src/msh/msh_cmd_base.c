#include <sys/_exit.h>
#include <sys/chdir.h>
#include <sys/write.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

int cmd_cd(struct sh* ctx, int argc, char** argv)
{
	int ret;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;

	return fchk(syschdir(argv[1]), ctx, "chdir", argv[1]);
}

int cmd_exec(struct sh* ctx, int argc, char** argv)
{
	int ret;

	if((ret = numargs(ctx, argc, 2, 0)))
		return ret;

	return fchk(execvpe(argv[1], argv+1, ctx->envp), ctx, "exec", argv[1]);
}

int cmd_exit(struct sh* ctx, int argc, char** argv)
{
	int ret, code = 0;

	if((ret = numargs(ctx, argc, 1, 2)))
		return ret;
	if(argc > 1 && (ret = argint(ctx, argv[1], &code)))
		return ret;

	_exit(code);
}

static int print(struct sh* ctx, int argc, char** argv, int fd)
{
	int ret;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;

	int len = strlen(argv[1]);
	argv[1][len] = '\n';
	syswrite(fd, argv[1], len+1);
	argv[1][len] = '\0';

	return 0;
}

int cmd_echo(struct sh* ctx, int argc, char** argv)
{
	return print(ctx, argc, argv, STDOUT);
}

int cmd_warn(struct sh* ctx, int argc, char** argv)
{
	return print(ctx, argc, argv, STDERR);
}

int cmd_die(struct sh* ctx, int argc, char** argv)
{
	cmd_warn(ctx, argc, argv);
	_exit(0xFF);
}
