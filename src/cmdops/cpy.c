#include <sys/dents.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/mman.h>
#include <sys/creds.h>
#include <sys/splice.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "cpy.h"

ERRTAG("cpy");

#define OPTS "nthomu"
#define OPT_n (1<<0)
#define OPT_t (1<<1)
#define OPT_h (1<<2)
#define OPT_o (1<<3)
#define OPT_m (1<<4)
#define OPT_u (1<<5)

/* Arguments parsing and tree walker invocation */

static int got_args(CTX)
{
	return ctx->argi < ctx->argc;
}

static void need_some_args(CTX)
{
	if(!got_args(ctx))
		fail("too few arguments", NULL, 0);
}

static void no_more_args(CTX)
{
	if(got_args(ctx))
		fail("too many arguments", NULL, 0);
}

static char* shift_arg(CTX)
{
	need_some_args(ctx);

	return ctx->argv[ctx->argi++];
}

static void copy_over(CTX, CCT)
{
	char* dst = shift_arg(ctx);
	char* src = shift_arg(ctx);
	no_more_args(ctx);

	runrec(cct, dst, src, DT_UNKNOWN);
}

static void copy_many(CTX, CCT)
{
	need_some_args(ctx);

	while(got_args(ctx)) {
		char* src = shift_arg(ctx);
		char* dst = basename(src);

		runrec(cct, dst, src, DT_UNKNOWN);
	}
}

static void open_target_dir(CTX, CCT)
{
	char* dst = shift_arg(ctx);
	int fd;

	if((fd = sys_open(dst, O_DIRECTORY)) < 0)
		fail(NULL, dst, fd);

	cct->dst.at = fd;
	cct->dst.dir = dst;
}

static int prep_opts(CTX, int argc, char** argv)
{
	int i = 1, opts = 0;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->argc = argc;
	ctx->argv = argv;
	ctx->argi = i;
	ctx->opts = opts;

	ctx->move = opts & OPT_m;
	ctx->newc = opts & OPT_n;
	ctx->user = opts & OPT_u;

	if(ctx->user) {
		ctx->uid = sys_getuid();
		ctx->gid = sys_getgid();
	}

	return opts;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	int opts = prep_opts(ctx, argc, argv);

	struct cct cct  = {
		.top = ctx,
		.src = { AT_FDCWD, NULL },
		.dst = { AT_FDCWD, NULL }
	};

	if(opts & OPT_t)
		open_target_dir(ctx, &cct);

	if(opts & (OPT_t | OPT_h))
		copy_many(ctx, &cct);
	else if(opts & (OPT_n | OPT_o))
		copy_over(ctx, &cct);
	else
		fail("no mode specified", NULL, 0);

	return 0;
}
