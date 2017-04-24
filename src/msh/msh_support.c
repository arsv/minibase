#include <bits/errno.h>
#include <sys/_exit.h>
#include <sys/brk.h>
#include <sys/open.h>
#include <sys/fstat.h>
#include <sys/close.h>
#include <sys/mmap.h>
#include <sys/munmap.h>

#include <string.h>
#include <format.h>
#include <null.h>
#include <util.h>

#include "msh.h"

/* Heap routines. See msh.h for heap layout. */

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

int mmapfile(struct mbuf* mb, char* name)
{
	int fd;
	long ret;
	struct stat st;

	if((fd = sysopen(name, O_RDONLY | O_CLOEXEC)) < 0)
		return fd;
	if((ret = sysfstat(fd, &st)) < 0)
		goto out;
	/* get larger-than-int files out of the picture */
	if(st.st_size > 0x7FFFFFFF) {
		ret = -E2BIG;
		goto out;
	}

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;

	ret = sysmmap(NULL, st.st_size, prot, flags, fd, 0);

	if(MMAPERROR(ret))
		goto out;

	mb->len = st.st_size;
	mb->buf = (char*)ret;
	ret = 0;
out:
	sysclose(fd);
	return ret;
}

int munmapfile(struct mbuf* mb)
{
	return sysmunmap(mb->buf, mb->len);
}
