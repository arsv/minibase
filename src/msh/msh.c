#include <sys/open.h>
#include <sys/read.h>

#include <string.h>
#include <util.h>

#include "msh.h"

static void parsefd(struct sh* ctx, int fd)
{
	char inbuf[2048];
	int rd;

	while((rd = sysread(fd, inbuf, sizeof(inbuf))) > 0) {
		parse(ctx, inbuf, rd);
	} if(rd < 0) {
		fail("read", NULL, rd);
	};
}

static void parsestr(struct sh* ctx, char* str)
{
	parse(ctx, str, strlen(str));
}

static int openfile(struct sh* ctx, char* name)
{
	int fd;

	if((fd = sysopen(name, O_RDONLY)) < 0)
		fail("open", name, fd);

	if(!ctx->file) {
		ctx->file = name;
		ctx->line = 1;
	}

	return fd;
}

#define OPT_c (1<<0)

static int parseopts(struct sh* ctx, char* str)
{
	char* p;
	int opts = 0;

	for(p = str; *p; p++) switch(*p) {
		case 'c': opts |= OPT_c; break;
		default: fatal(ctx, "unknown options", str);
	}

	return opts;
}

int main(int argc, char** argv, char** envp)
{
	struct sh ctx;
	long fd = STDIN;
	char* script = NULL;
	int opts = 0;
	int i = 1;

	memset(&ctx, 0, sizeof(ctx));
	ctx.envp = envp;
	ctx.line = 1;

	if(i < argc && argv[i][0] == '-')
		opts = parseopts(&ctx, argv[i++] + 1);
	if(i < argc)
		script = argv[i++];
	if(!(opts & OPT_c) && script)
		fd = openfile(&ctx, script);

	hinit(&ctx);

	if(!(opts & OPT_c))
		parsefd(&ctx, fd);
	else
		parsestr(&ctx, script);

	pfini(&ctx);

	return 0;
}
