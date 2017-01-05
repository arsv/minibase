#include <sys/open.h>
#include <sys/read.h>

#include <string.h>
#include <util.h>

#include "msh.h"

char inbuf[4096];

int main(int argc, char** argv, char** envp)
{
	struct sh ctx;
	long rd;
	long fd = STDIN;

	memset(&ctx, 0, sizeof(ctx));
	ctx.envp = envp;
	ctx.line = 1;

	if(argc > 1)
		if((fd = sysopen(ctx.file = argv[1], O_RDONLY)) < 0)
			fail("open", argv[1], fd);

	hinit(&ctx);

	while((rd = sysread(fd, inbuf, sizeof(inbuf))) > 0) {
		parse(&ctx, inbuf, rd);
	} if(rd < 0) {
		fail("read", NULL, rd);
	};

	pfini(&ctx);

	return 0;
}
