#include <sys/fork.h>
#include <sys/_exit.h>
#include <sys/stat.h>
#include <sys/waitpid.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

static const struct cmd {
	char name[12];
	int (*func)(struct sh* ctx, int argc, char** argv);
} builtin[] = {
#define CMD(name) \
	{ #name, cmd_##name },
#include "msh_cmd.h"
	{ "", NULL }
};

static int leadingdash(char** argv)
{
	int ret = (argv[0][0] == '-');

	if(ret) argv[0]++;

	return ret;
}

static int spawn(struct sh* ctx, int argc, char** argv)
{
	long pid = sysfork();
	int status;

	if(pid < 0)
		fail("fork", NULL, pid);

	if(!pid) {
		long ret = execvpe(*argv, argv, ctx->envp);
		error(ctx, "exec", *argv, ret);
		_exit(0xFF);
	}

	if((pid = syswaitpid(pid, &status, 0)) < 0)
		fail("wait", *argv, pid);

	return status;
}

static int command(struct sh* ctx, int argc, char** argv)
{
	const struct cmd* cc;

	for(cc = builtin; cc->func; cc++)
		if(!strncmp(cc->name, argv[0], sizeof(cc->name)))
			break;

	if(cc->func)
		return cc->func(ctx, argc, argv);
	else
		return spawn(ctx, argc, argv);
}

/* Flow control commands */

static int cond_defined(struct sh* ctx, char** args)
{
	return valueof(ctx, args[0]) != 0 ? 0 : 1;
}

static int cond_exists(struct sh* ctx, char** args)
{
	struct stat st;

	return sysstat(args[0], &st) >= 0 ? 0 : 1;
}

static const struct cond {
	char name[8];
	int (*func)(struct sh* ctx, char** args);
	int nargs;
} conds[] = {
	{ "defined",  cond_defined, 1 },
	{ "exists",   cond_exists,  1 },
	{ "",         NULL,         0 }
};

static int condition(struct sh* ctx, int argc, char** argv)
{
	const struct cond* cd;

	if(argc < 2)
		fatal(ctx, "missing condition", NULL);

	for(cd = conds; cd->func; cd++)
		if(!strncmp(cd->name, argv[1], sizeof(cd->name)))
			break;
	if(!cd->func)
		fatal(ctx, "unknown condition", argv[1]);
	if(argc != 2 + cd->nargs)
		fatal(ctx, "invalid condition", NULL);

	return cd->func(ctx, argv + 2);
}

static void cmd_if(struct sh* ctx, int argc, char** argv)
{
	int cond = ctx->cond;
	int prev = cond;

	cond = (cond << CSHIFT) | CHADIF;

	if(prev & CGUARD)
		fatal(ctx, "too many nested conditionals", NULL);
	if(prev & CSKIP)
		cond |= CSKIP;
	else if(condition(ctx, argc, argv))
		cond |= CHADTRUE;
	else
		cond |= CSKIP;

	ctx->cond = cond;
}

static void cmd_elif(struct sh* ctx, int argc, char** argv)
{
	int cond = ctx->cond;

	if(!(cond & CHADIF))
		fatal(ctx, "misplaced elif", NULL);
	if((cond >> CSHIFT) & CSKIP)
		cond |= CSKIP;
	else if(cond & CHADTRUE)
		cond |= CSKIP;
	else if(condition(ctx, argc, argv))
		cond |= CHADTRUE;
	else
		cond |= CSKIP;

	ctx->cond = cond;
}

static void cmd_else(struct sh* ctx, int argc, char** argv)
{
	int cond = ctx->cond;

	if(argc > 1)
		fatal(ctx, "no arguments allowed", NULL);
	if(!(cond & CHADIF) || (cond & CHADELSE))
		fatal(ctx, "misplaced else", NULL);
	if((cond >> CSHIFT) & CSKIP)
		cond |= CSKIP;
	else if(cond & CHADTRUE)
		cond |= CSKIP;
	else
		cond |= CHADELSE;

	ctx->cond = cond;
}

static void cmd_fi(struct sh* ctx, int argc, char** argv)
{
	int cond = ctx->cond;

	if(argc > 1)
		fatal(ctx, "no arguments allowed", NULL);
	if(!(cond & CHADIF))
		fatal(ctx, "misplaced fi", NULL);

	ctx->cond = (cond >> CSHIFT);
}

static const struct flc {
	char name[8];
	void (*func)(struct sh* ctx, int argc, char** argv);
} flowctls[] = {
	{ "if",       cmd_if   },
	{ "else",     cmd_else },
	{ "elif",     cmd_elif },
	{ "fi",       cmd_fi   },
	{ "",         NULL     }
};

static int flowcontrol(struct sh* ctx, int argc, char** argv)
{
	const struct flc* fc;

	for(fc = flowctls; fc->func; fc++)
		if(!strncmp(fc->name, argv[0], sizeof(fc->name)))
			break;
	if(!fc->func)
		return 0;

	fc->func(ctx, argc, argv);

	return 1;
}

void statement(struct sh* ctx, int argc, char** argv)
{
	if(flowcontrol(ctx, argc, argv))
		return;
	if(ctx->cond & CSKIP)
		return;

	int noerror = leadingdash(argv);

	if(command(ctx, argc, argv) && !noerror)
		fatal(ctx, "command failed", NULL);
}
