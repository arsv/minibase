#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/sync.h>
#include <string.h>
#include <output.h>
#include <format.h>
#include <util.h>

#include "shell.h"

static int parse_mode(char* str, int* mod)
{
	char* p;
	int len = strlen(str);

	if(len != 4 && len != 5)
		return -1;
	if(*str != '0')
		return -1;

	if(!(p = parseoct(str, mod)) || *p)
		return -1;

	return 0;
}

static int parse_uid(char* str, int* id)
{
	char* p;

	if(!(p = parseint(str, id)) || *p)
		return -1;

	return 0;
}

void cmd_chmod(void)
{
	char* name;
	char* mstr;
	int ret, mode;

	if(!(name = shift_arg()))
		return;
	if(!(mstr = shift_arg()))
		return;
	if(extra_arguments())
		return;

	if(parse_mode(mstr, &mode))
		return repl("invalid mode", NULL, 0);

	if((ret = sys_chmod(name, mode)) < 0)
		return repl(NULL, NULL, ret);
}

void cmd_chown(void)
{
	char* name;
	char* user;
	char* group;
	int ret, uid, gid;

	if(!(name = shift_arg()))
		return;
	if(!(user = shift_arg()))
		return;
	if(!(group = shift_arg()))
		return;
	if(extra_arguments())
		return;

	if(parse_uid(user, &uid))
		return repl("invalid uid", NULL, 0);
	if(parse_uid(group, &gid))
		return repl("invalid gid", NULL, 0);

	if((ret = sys_lchown(name, uid, gid)) < 0)
		return repl(NULL, NULL, ret);
}

void cmd_symlink(void)
{
	char* name;
	char* target;
	int ret;

	if(!(name = shift_arg()))
		return;
	if(!(target = shift_arg()))
		return;
	if(extra_arguments())
		return;

	if((ret = sys_symlink(name, target)) < 0)
		return repl(NULL, NULL, ret);
}

void cmd_sync(void)
{
	int ret;

	if((ret = sys_sync()))
		return repl(NULL, NULL, ret);
}

void cmd_rmdir(void)
{
	int ret;
	char* name;

	if(!(name = shift_arg()))
		return;
	if((ret = sys_rmdir(name)) < 0)
		return repl(NULL, NULL, ret);
}

void cmd_unlink(void)
{
	int ret;
	char* name;

	if(!(name = shift_arg()))
		return;
	if((ret = sys_unlink(name)))
		return warn(NULL, NULL, ret);
}

static void echo(char* str)
{
	int len = strlen(str);

	if(len >= 2048)
		return;

	int size = len + 10;
	char* buf = alloca(size);
	char* p = buf;
	char* e = buf + len + size - 1;

	p = fmtstr(p, e, "> ");
	p = fmtstr(p, e, str);
	p = fmtchar(p, e, '\n');

	writeall(STDOUT, buf, p - buf);
}

void cmd_echo(void)
{
	char* arg;

	while((arg = shift()))
		echo(arg);
}

void cmd_pwd(void)
{
	int size = 4096;
	char* buf = heap_alloc(size);
	int ret;

	char* p = buf;
	char* e = buf + size - 1;

	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ": ");

	if((ret = sys_getcwd(p, e - p)) < 0)
		return repl("getcwd", NULL, ret);

	p += ret;
	*p++ = '\n';

	output(buf, p - buf);
}

void cmd_cd(void)
{
	char* dir;
	int ret;

	if(!(dir = shift_arg()))
		return;
	if(extra_arguments())
		return;

	if((ret = sys_chdir(dir)) < 0)
		return warn(NULL, NULL, ret);

	cmd_pwd();
}

void cmd_up(void)
{
	char* dir = "..";
	int ret;

	if(extra_arguments())
		return;

	if((ret = sys_chdir(dir)) < 0)
		return warn(NULL, NULL, ret);

	cmd_pwd();
}

static const struct cmd {
	char name[8];
	void (*call)(void);
} commands[] = {
	{ "echo",     cmd_echo    },

	{ "pwd",      cmd_pwd     },
	{ "cd",       cmd_cd      },
	{ "up",       cmd_up      },

	{ "chown",    cmd_chown   },
	{ "chmod",    cmd_chmod   },
	{ "unlink",   cmd_unlink  },
	{ "rmdir",    cmd_rmdir   },
	{ "symlink",  cmd_symlink },

	{ "ls",       cmd_ls      },  /* list files and dirs */
	{ "la",       cmd_la      },  /* list all files (dotted/non-dotted) */
	{ "ld",       cmd_ld      },  /* list directories */
	{ "lf",       cmd_lf      },
	{ "lx",       cmd_lx      },  /* list executable */

	{ "lh",       cmd_lh      },  /* list hidden entries */
	{ "lhf",      cmd_lhf     },  /* list hidden files */
	{ "lhd",      cmd_lhd     },  /* list hidden directories */

	{ "stat",     cmd_stat    },  /* file stat */
	{ "time",     cmd_time    },
	{ "info",     cmd_info    },

	//{ "mntstat",  cmd_mntstat },
	//{ "mount",    cmd_mount   },
	//{ "umount",   cmd_umount  },
	{ "sync",     cmd_sync    },

	{ "write",    cmd_write   },

	{ "ps",       cmd_ps      }, /* list processes */
	{ "kill",     cmd_kill    }, /* child, or by pid, or by name? */

	//{ "find", cmd_find }, /* maybe ??? */
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
		return warn("empty line", NULL, 0);

	if(!(cc = find_command(comm)))
		return warn("unknown command", comm, 0);

	cc->call();
}
