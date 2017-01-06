#include <sys/brk.h>
#include <sys/_exit.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"

#define PAGE 4096

void hinit(struct sh* ctx)
{
	void* heap = (void*)sysbrk(0);
	ctx->heap = heap;
	ctx->esep = NULL;
	ctx->csep = heap;
	ctx->hptr = heap;
	ctx->hend = (void*)sysbrk(heap + 4096);
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

void hrev(struct sh* ctx, int what)
{
	switch(what) {
		case VSEP:
			ctx->hptr = ctx->var;
			ctx->var = NULL;
			break;
		case CSEP:
			ctx->hptr = ctx->csep;
			break;
		case ESEP:
			ctx->csep = NULL;
			ctx->hptr = ctx->esep;
			break;
		case HEAP:
			ctx->esep = NULL;
			ctx->csep = NULL;
			ctx->hptr = ctx->heap;
	}
}

void hset(struct sh* ctx, int what)
{
	switch(what) {
		case CSEP: ctx->csep = ctx->hptr; break;
		case ESEP: ctx->esep = ctx->hptr; break;
		case VSEP: ctx->var = ctx->hptr; break;
	}
}
