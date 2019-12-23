#include <sys/file.h>
#include <sys/proc.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

/* Common routines used by most commands (and by some other parts
   of the code as well).

   Library quit() and warn() are not very well suited for msh,
   which should preferably use script name and line much more
   often than generic "msh:" tag. */

static void report(CTX, const char* err, char* arg, long ret)
{
	FMTBUF(p, e, buf, 512);

	if(ctx->file) {
		p = fmtstr(p, e, ctx->file);
		p = fmtstr(p, e, ":");
		p = fmtint(p, e, ctx->line);
		p = fmtstr(p, e, ":");
	} else {
		p = fmtstr(p, e, "msh:");
	}

	if(err) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, err);
	} if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	} if((arg || err) && ret) {
		p = fmtstr(p, e, ": ");
	} if(ret) {
		p = fmterr(p, e, ret);
	}

	FMTENL(p, e);

	writeall(ctx->errfd, buf, p - buf);
}

/* In case `onexit` has been used, invoke it when exiting. */

void exit(CTX, int code)
{
	int ret;

	if(*ctx->trap) {
		char* argv[] = { ctx->trap, NULL };

		ret = sys_execve(*argv, argv, ctx->envp);

		ctx->file = NULL;

		report(ctx, "exec", *argv, ret);
	}

	_exit(code);
}

/* For a few cases where error is related to msh dealing with the script
   file itself, "msh: error message" reports are better. */

void quit(CTX, const char* err, char* arg, int ret)
{
	ctx->file = NULL; /* force msh: tag */
	report(ctx, err, arg, ret);
	exit(ctx, 0xFF);
}

/* These two do the same exact thing, error() takes errno and fatal() does not.
   Originally there was a distinction that did warrant fatal() to be named like
   that (error was recoverable and fatal was not), but the whole thing has been
   reworked since and fatal() has been kept as just a handy shortcut. */

void error(CTX, const char* err, char* arg, int ret)
{
	report(ctx, err, arg, ret);
	exit(ctx, 0xFF);
}

void fatal(CTX, const char* err, char* arg)
{
	report(ctx, err, arg, 0);
	exit(ctx, 0xFF);
}

/* Arguments handling for builtins */

void check(CTX, const char* msg, char* arg, int ret)
{
	if(ret < 0) error(ctx, msg, arg, ret);
}

int got_more_arguments(CTX)
{
	return (ctx->argp < ctx->argc);
}

void no_more_arguments(CTX)
{
	if(ctx->argp < ctx->argc)
		fatal(ctx, "too many arguments", NULL);
}

void need_some_arguments(CTX)
{
	if(ctx->argp >= ctx->argc)
		fatal(ctx, "argument required", NULL);
}

char** argsleft(CTX)
{
	return &(ctx->argv[ctx->argp]);
}

char* next(CTX)
{
	if(ctx->argp >= ctx->argc)
		return NULL;

	return ctx->argv[ctx->argp++];
}

char* shift(CTX)
{
	if(ctx->argp >= ctx->argc)
		fatal(ctx, "too few arguments", NULL);

	return ctx->argv[ctx->argp++];
}

char* dash_opts(CTX)
{
	if(ctx->argp >= ctx->argc)
		return NULL;

	char* lead = ctx->argv[ctx->argp];

	if(*lead != '-')
		return NULL;

	ctx->argp++;

	return lead + 1;
}

void shift_int(CTX, int* dst)
{
	char* str = shift(ctx);
	char* p;

	if(!(p = parseint(str, dst)) || *p)
		fatal(ctx, "invalid number:", str);
}

void shift_u64(CTX, uint64_t* dst)
{
	char* str = shift(ctx);
	char* p;

	if(!(p = parseu64(str, dst)) || *p)
		fatal(ctx, "invalid number:", str);
}

void shift_oct(CTX, int* dst)
{
	char* str = shift(ctx);
	char* p;

	if(*str != '0')
		fatal(ctx, "invalid octal:", str);
	if(!(p = parseoct(str, dst)) || *p)
		fatal(ctx, "invalid octal:", str);
}

/* Command lookup code */

static const struct cmd {
	char name[12];
	void (*func)(CTX);
} builtins[] = {
#define CMD(name) \
	{ #name, cmd_##name },
#include "msh_cmd.h"
};

static const struct cmd* builtin(const char* name)
{
	const struct cmd* cc;
	int maxlen = sizeof(cc->name);

	for(cc = builtins; cc < ARRAY_END(builtins); cc++)
		if(!strncmp(cc->name, name, maxlen))
			return cc;

	return NULL;
}

void command(CTX)
{
	const struct cmd* cc;

	if(!ctx->argc)
		return;

	char* lead = ctx->argv[0];

	if(!(cc = builtin(lead)))
		fatal(ctx, "unknown command", lead);

	return cc->func(ctx);
}
