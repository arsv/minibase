#include <string.h>
#include <format.h>
#include <errtag.h>
#include <exit.h>
#include <util.h>

#include "msh.h"

ERRTAG = "msh";
ERRLIST = {
	REPORT(ENOENT), REPORT(ENOTDIR), REPORT(EISDIR), REPORT(EACCES),
	REPORT(EPERM), REPORT(EFAULT), REPORT(EBADF), { 0, NULL }
};

/* Common fail() and warn() are not very well suited for msh,
   which should preferably use script name and line much more
   often than generic msh: tag, and sometimes maybe even
   impersonate built-in commands. */

static int maybelen(const char* str)
{
	return str ? strlen(str) : 0;
}

/* Cannot use heap here, unless halloc is changed to never cause
   or report errors. */

#define TAGGED_SAVED     0
#define FILE_LINE_SAVED  1
#define FILE_LINE_REDIR  2

static void report(struct sh* ctx, const char* err, char* arg, long ret, int m)
{
	char* file = ctx->file;
	int line = ctx->line;
	int len = maybelen(file) + maybelen(err) + maybelen(arg) + 50;
	int fd;

	FMTBUF(p, e, buf, len);

	if(m == TAGGED_SAVED) {
		p = fmtstr(p, e, errtag);
	} else {
		p = fmtstr(p, e, file);
		p = fmtstr(p, e, ":");
		p = fmtint(p, e, line);
	}

	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, err);

	if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	} if(ret) {
		p = fmtstr(p, e, ": ");
		p = fmterr(p, e, ret);
	}

	FMTENL(p);

	if(m == FILE_LINE_REDIR)
		fd = STDERR;
	else
		fd = ctx->errfd;

	writeall(fd, buf, p - buf);
}

void fail(struct sh* ctx, const char* err, char* arg, long ret)
{
	report(ctx, err, arg, ret, TAGGED_SAVED);
	_exit(0xFF);
}

int error(struct sh* ctx, const char* err, char* arg, long ret)
{
	report(ctx, err, arg, ret, FILE_LINE_REDIR);
	return -1;
}

void fatal(struct sh* ctx, const char* err, char* arg)
{
	report(ctx, err, arg, 0, FILE_LINE_SAVED);
	_exit(0xFF);
}

int fchk(long ret, struct sh* ctx, char* arg)
{
	if(ret < 0)
		return error(ctx, *ctx->argv, arg, ret);
	else
		return 0;
}

/* Arguments handling for builtins */

int numleft(struct sh* ctx)
{
	return ctx->argc - ctx->argp;
}

int dasharg(struct sh* ctx)
{
	char* arg = peek(ctx);

	return arg && *arg == '-';
}

int moreleft(struct sh* ctx)
{
	if(numleft(ctx) > 0)
		return error(ctx, "too many arguments", NULL, 0);
	else
		return 0;
}

int noneleft(struct sh* ctx)
{
	if(numleft(ctx) <= 0)
		return error(ctx, "too few arguments", NULL, 0);
	else
		return 0;
}

char** argsleft(struct sh* ctx)
{
	return &(ctx->argv[ctx->argp]);
}

char* peek(struct sh* ctx)
{
	if(ctx->argp < ctx->argc)
		return ctx->argv[ctx->argp];
	else
		return NULL;
}

char* shift(struct sh* ctx)
{
	char* arg;

	if((arg = peek(ctx)))
		ctx->argp++;

	return arg;
}

int shift_str(struct sh* ctx, char** dst)
{
	char* str;

	if(!(str = shift(ctx)))
		return error(ctx, "argument required", NULL, 0);

	*dst = str;
	return 0;
}

static int argument_required(struct sh* ctx)
{
	return error(ctx, "argument required", NULL, 0);
}

static int numeric_arg_required(struct sh* ctx)
{
	return error(ctx, "numeric argument required", NULL, 0);
}

int shift_int(struct sh* ctx, int* dst)
{
	char* p;

	if(!(p = shift(ctx)))
		return argument_required(ctx);
	if(!(p = parseint(p, dst)) || *p)
		return numeric_arg_required(ctx);

	return 0;
}

int shift_u64(struct sh* ctx, uint64_t* dst)
{
	char* p;

	if(!(p = shift(ctx)))
		return argument_required(ctx);
	if(!(p = parseu64(p, dst)) || *p)
		return numeric_arg_required(ctx);

	return 0;
}

int shift_oct(struct sh* ctx, int* dst)
{
	char* p;

	if(!(p = shift(ctx)))
		return argument_required(ctx);
	if(*p != '0')
		return error(ctx, "mode must be octal", NULL, 0);
	if(!(p = parseoct(p, dst)) || *p)
		return numeric_arg_required(ctx);

	return 0;
}
