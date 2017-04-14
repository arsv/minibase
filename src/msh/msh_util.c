#include <bits/errno.h>
#include <sys/brk.h>
#include <sys/_exit.h>

#include <string.h>
#include <format.h>
#include <null.h>
#include <util.h>

#include "msh.h"

static const char tag[] = "msh";
static const struct errcode {
	short code;
	char* name;
} elist[] = {
#define REPORT(e) { e, #e }
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

static char* fmterr(char* buf, char* end, int err)
{
	const struct errcode* p;

	err = -err;

	for(p = elist; p->code; p++)
		if(p->code == err)
			break;
	if(p->code)
		return fmtstr(buf, end, p->name);
	else
		return fmti32(buf, end, err);
};

/* Cannot use heap here, unless halloc is changed to never cause
   or report errors. */

void report(const char* file, int line, const char* err, char* arg, long ret)
{
	int len = maybelen(file) + maybelen(err) + maybelen(arg) + 50;

	char buf[len];
	char* p = buf;
	char* e = buf + sizeof(buf);

	if(file) {
		p = fmtstr(p, e, file);
		p = fmtstr(p, e, ":");
	} if(line) {
		p = fmtint(p, e, line);
		p = fmtstr(p, e, ":");
	} if(file || line) {
		p = fmtstr(p, e, " ");
	}

	p = fmtstr(p, e, err);

	if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	} if(ret) {
		p = fmtstr(p, e, ": ");
		p = fmterr(p, e, ret);
	}

	*p++ = '\n';

	writeall(STDERR, buf, p - buf);
}

void fail(const char* err, char* arg, long ret)
{
	report(tag, 0, err, arg, ret);
	_exit(0xFF);
}

int error(struct sh* ctx, const char* err, char* arg, long ret)
{
	report(ctx->file, ctx->line, err, arg, ret);
	return -1;
}

void fatal(struct sh* ctx, const char* err, char* arg)
{
	report(ctx->file, ctx->line, err, arg, 0);
	_exit(0xFF);
}

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
