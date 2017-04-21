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
	int (*func)(struct sh* ctx);
} builtin[] = {
#define CMD(name) \
	{ #name, cmd_##name },
#include "msh_cmd.h"
	{ "", NULL }
};

static int spawn(struct sh* ctx)
{
	long pid = sysfork();
	char* cmd = *ctx->argv;
	int status;

	if(pid < 0)
		fail("fork", NULL, pid);

	if(!pid) {
		long ret = execvpe(cmd, ctx->argv, ctx->envp);
		error(ctx, "exec", cmd, ret);
		_exit(0xFF);
	}

	if((pid = syswaitpid(pid, &status, 0)) < 0)
		fail("wait", cmd, pid);

	return status;
}

static int command(struct sh* ctx)
{
	const struct cmd* cc;

	for(cc = builtin; cc->func; cc++)
		if(!strncmp(cc->name, *ctx->argv, sizeof(cc->name)))
			break;

	if(cc->func)
		return cc->func(ctx);
	else
		return spawn(ctx);
}

/* Flow control commands.
   
   The whole thing with condition should probably look different,
   but how exactly is not clear atm. It's tempting to do some sort
   of && and || and return code checks for arbitrary commands, but
   actual usage is going to be limited to a single check per if
   and just a couple of predicates apparently (def, set, nexist).

   Either way it seems like a bad idea to treat conditions as
   generic commands unless explicitly told to, i.e. allow
   "if grep ..." and similar.

   Oh and the body will be one-liner in lots of cases.
   Maybe the syntax should reflect that. Maybe not. */

static int cond_def(struct sh* ctx)
{
	return valueof(ctx, shift(ctx)) != 0;
}

static int cond_set(struct sh* ctx)
{
	char* val = valueof(ctx, shift(ctx));

	return val && *val;
}

static int cond_exists(struct sh* ctx)
{
	struct stat st;

	return sysstat(shift(ctx), &st) >= 0;
}

static int cond_nexist(struct sh* ctx)
{
	return !cond_exists(ctx);
}

static const struct cond {
	char name[8];
	int (*func)(struct sh* ctx);
	int nargs;
} conds[] = {
	{ "def",     cond_def,     1 },
	{ "set",     cond_set,     1 },
	{ "exists",  cond_exists,  1 },
	{ "nexist",  cond_nexist,  1 },
	{ "",        NULL,         0 }
};

static int condition(struct sh* ctx)
{
	const struct cond* cd;
	char* key = shift(ctx);

	if(!key)
		fatal(ctx, "missing condition", NULL);

	for(cd = conds; cd->func; cd++)
		if(!strncmp(cd->name, key, sizeof(cd->name)))
			break;
	if(!cd->func)
		fatal(ctx, "unknown condition", key);
	if(numleft(ctx) != cd->nargs)
		fatal(ctx, "invalid condition", NULL);

	return cd->func(ctx);
}

static void cmd_if(struct sh* ctx)
{
	int cond = ctx->cond;
	int prev = cond;

	cond = (cond << CSHIFT) | CHADIF;

	if(prev & CGUARD)
		fatal(ctx, "too many nested conditionals", NULL);
	if(prev & CSKIP)
		cond |= CSKIP;
	else if(condition(ctx))
		cond |= CHADTRUE;
	else
		cond |= CSKIP;

	ctx->cond = cond;
}

static void cmd_elif(struct sh* ctx)
{
	int cond = ctx->cond;

	if(!(cond & CHADIF))
		fatal(ctx, "misplaced elif", NULL);
	if((cond >> CSHIFT) & CSKIP)
		cond |= CSKIP;
	else if(cond & CHADTRUE)
		cond |= CSKIP;
	else if(condition(ctx))
		cond |= CHADTRUE;
	else
		cond |= CSKIP;

	ctx->cond = cond;
}

static void cmd_else(struct sh* ctx)
{
	int cond = ctx->cond;

	if(ctx->argc > 1)
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

static void cmd_fi(struct sh* ctx)
{
	int cond = ctx->cond;

	if(ctx->argc > 1)
		fatal(ctx, "no arguments allowed", NULL);
	if(!(cond & CHADIF))
		fatal(ctx, "misplaced fi", NULL);

	ctx->cond = (cond >> CSHIFT);
}

static const struct flc {
	char name[8];
	void (*func)(struct sh* ctx);
} flowctls[] = {
	{ "if",       cmd_if   },
	{ "else",     cmd_else },
	{ "elif",     cmd_elif },
	{ "fi",       cmd_fi   },
	{ "",         NULL     }
};

static int flowcontrol(struct sh* ctx)
{
	const struct flc* fc;

	for(fc = flowctls; fc->func; fc++)
		if(!strncmp(fc->name, *ctx->argv, sizeof(fc->name)))
			break;
	if(!fc->func)
		return 0;

	fc->func(ctx);

	return 1;
}

static int leadingdash(char** argv)
{
	int ret = (argv[0][0] == '-');

	if(ret) argv[0]++;

	return ret;
}

void statement(struct sh* ctx)
{
	if(!ctx->argc)
		return;
	if(flowcontrol(ctx))
		return;
	if(ctx->cond & CSKIP)
		return;

	int noerror = leadingdash(ctx->argv);

	if(command(ctx) && !noerror)
		fatal(ctx, "command failed", NULL);
}
