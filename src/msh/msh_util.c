#include <sys/brk.h>
#include <sys/_exit.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"

#define PAGE 4096

void hinit(struct sh* ctx)
{
	ctx->heap = (void*)sysbrk(0);
	ctx->hend = (void*)sysbrk(ctx->heap + 4096);
	ctx->hptr = ctx->heap;
}

void* halloc(struct sh* ctx, int len)
{
	void* ret = ctx->hptr;

	if(ctx->hptr + len < ctx->hend)
		goto ptr;

	int spc = ctx->hend - ctx->hptr - len;
	spc += (PAGE - spc % PAGE) % PAGE;
	ctx->hend = (void*)sysbrk(ctx->hend + spc);

	if(ctx->hptr + len < ctx->hend)
		fail("cannot allocate memory", NULL, 0);
ptr:
	ctx->hptr += len;

	return ret;
}
