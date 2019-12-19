#include <sys/file.h>
#include <string.h>
#include <util.h>
#include <main.h>

#include "msh.h"

ERRTAG("msh");

static void parsefile(CTX, char* name)
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
}

static void parsestr(CTX, char* str)
{
	parse(ctx, str, strlen(str));
}

#define OPT_c (1<<0)

static int parseopts(CTX, char* str)
{
	char* p;
	int opts = 0;

	for(p = str; *p; p++) switch(*p) {
		case 'c': opts |= OPT_c; break;
		default: fatal(ctx, "unknown options", str);
	}

	return opts;
}

int main(int argc, char** argv)
{
	struct sh ctx;
	char* script = NULL;
	int opts = 0;
	int i = 1;

	memset(&ctx, 0, sizeof(ctx));
	ctx.envp = argv + argc + 1;
	ctx.errfd = STDERR;

	if(i < argc && argv[i][0] == '-')
		opts = parseopts(&ctx, argv[i++] + 1);
	if(i >= argc)
		fatal(&ctx, "script name required", NULL);

	script = argv[i++];

	ctx.topargc = argc;
	ctx.topargp = i;
	ctx.topargv = argv;

	hinit(&ctx);

	if(!(opts & OPT_c))
		parsefile(&ctx, script);
	else
		parsestr(&ctx, script);

	pfini(&ctx);

	exit(&ctx, 0);
}
