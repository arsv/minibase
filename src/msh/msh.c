#include <sys/file.h>
#include <string.h>
#include <util.h>
#include <main.h>

#include "msh.h"

static void parse_file(CTX, char* name)
{
	char inbuf[2048];
	int fd, rd;
	int flags = O_RDONLY | O_CLOEXEC;

	if((fd = sys_open(name, flags)) < 0)
		quit(ctx, NULL, name, fd);

	ctx->file = name;
	ctx->line = 1;

	while((rd = sys_read(fd, inbuf, sizeof(inbuf))) > 0) {
		parse(ctx, inbuf, rd);
	} if(rd < 0) {
		quit(ctx, "read", NULL, rd);
	};

	parse_finish(ctx);
}

static void parse_str(CTX, char* str)
{
	parse(ctx, str, strlen(str));

	parse_finish(ctx);
}

#define OPT_c (1<<0)

static int parseopts(CTX, char* str)
{
	if(!*str)
		return 0;
	if(*str == 'c')
		return OPT_c;

	fatal(ctx, "unknown options", str);
}

int main(int argc, char** argv)
{
	struct sh context, *ctx = &context;
	char* script = NULL;
	int opts = 0;
	int i = 1;

	memzero(ctx, sizeof(*ctx));

	ctx->environ = argv + argc + 1;
	ctx->errfd = STDERR;
	ctx->sigfd = -1;

	if(i < argc && argv[i][0] == '-')
		opts = parseopts(ctx, argv[i++] + 1);
	if(i >= argc)
		fatal(ctx, "script name required", NULL);

	script = argv[i++];

	ctx->topargc = argc;
	ctx->topargp = i;
	ctx->topargv = argv;
	ctx->customenvp = -1;

	heap_init(ctx);

	if(!(opts & OPT_c))
		parse_file(ctx, script);
	else
		parse_str(ctx, script);

	exit(ctx, 0);
}
