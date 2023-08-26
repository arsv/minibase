#include <sys/fpath.h>
#include <string.h>
#include <output.h>
#include <util.h>

#include "shell.h"

void cmd_echo(void)
{
	char* arg;

	while((arg = shift())) {
		outstr("> ");
		outstr(arg);
		outstr("\n");
	}
}

void cmd_pwd(void)
{
	int ret;
	char* buf = tmpbuf;
	int max = sizeof(tmpbuf);

	if((ret = sys_getcwd(buf, max)) < 0)
		return warn("getcwd", NULL, ret);

	outstr("# cwd ");
	output(buf, ret);
	outstr("\n");
}

void cmd_cd(void)
{
	int ret;

	if(argcount != 2)
		return warn("invalid arguments", NULL, 0);

	char* dir = args[1];

	if((ret = sys_chdir(dir)) < 0)
		return warn("chdir", NULL, ret);

	cmd_pwd();
}

static const struct cmd {
	char name[8];
	void (*call)(void);
} commands[] = {
	{ "echo",  cmd_echo },
	{ "pwd",   cmd_pwd  },
	{ "cd",    cmd_cd   }
};

static const struct cmd* find_command(char* name)
{
	const struct cmd* cc;

	for(cc = commands; cc < ARRAY_END(commands); cc++)
		if(!strcmpn(cc->name, name, sizeof(cc->name)))
			return cc;

	return NULL;
}

void run_command(void)
{
	const struct cmd* cc;
	char* comm;

	if(!(comm = shift())) /* empty line */
		return;

	if(!(cc = find_command(comm)))
		return warn("unknown command", comm, 0);

	cc->call();
}
