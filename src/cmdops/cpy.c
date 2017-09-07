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

	run(ctx, cct, dst, src);
}

static void copy_many(CTX, CCT)
{
	need_some_args(ctx);

	while(got_args(ctx)) {
		char* src = shift_arg(ctx);
		char* dst = basename(src);

		run(ctx, cct, dst, src);
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

/* Straightforward copy implementation would fail() on the first error
   encountered, potentially after spending some time to copy half of
   the files requested. That's not good. To mend this, we do a dry run
   first, trying to catch obvious errors like non-readable files or
   non-enterable directories, and only then start to copy the files.

   Dry run results are not reliable, their purpose is to warn the user
   early and save some time/trouble if possible. The real run will still
   check for errors and may fail at any point. */

static void tryrun(void (*run)(CTX, CCT), CTX, CCT)
{
	int argi = ctx->argi;

	ctx->dryrun = 1;

	run(ctx, cct);

	if(ctx->errors)
		fail("aborting with no files copied", NULL, 0);

	ctx->dryrun = 0;
	ctx->argi = argi;

	run(ctx, cct);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	int opts = prep_opts(ctx, argc, argv);

	struct cct cct = {
		.top = ctx,
		.src = { AT_FDCWD, NULL, NULL, -1 },
		.dst = { AT_FDCWD, NULL, NULL, -1 }
	};

	if(opts & OPT_t)
		open_target_dir(ctx, &cct);

	if(opts & (OPT_t | OPT_h))
		tryrun(copy_many, ctx, &cct);
	else if(opts & (OPT_n | OPT_o))
		tryrun(copy_over, ctx, &cct);
	else
		fail("no mode specified", NULL, 0);

	return 0;
}
