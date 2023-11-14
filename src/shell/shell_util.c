#include <sys/file.h>
#include <sys/mman.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "shell.h"

char* shift(void)
{
	int idx = sh.argidx;
	int cnt = sh.argcnt;

	if(idx >= cnt)
		return NULL;

	char* arg = sh.args[idx];

	sh.argidx = idx + 1;

	return arg;
}

char* shift_arg(void)
{
	char* arg;

	if(!(arg = shift()))
		repl("argument required", NULL, 0);

	return arg;
}

char* shift_opt(void)
{
	int idx = sh.argidx;
	int cnt = sh.argcnt;

	if(idx >= cnt)
		return NULL;

	char* arg = sh.args[idx];

	if(*arg != '-')
		return NULL;

	sh.argidx = idx + 1;

	return arg + 1;
}

int got_more_args(void)
{
	int idx = sh.argidx;
	int cnt = sh.argcnt;

	return (idx < cnt);
}

int extra_arguments(void)
{
	char* arg;

	if((arg = shift()))
		repl("extra arguments", NULL, 0);

	return !!arg;
}

void check_close(int fd)
{
	int ret;

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);
}

/* Heap stuff */

static void heap_set(void* brk)
{
	int ret;

	brk = (void*)sys_brk(brk);

	if((ret = mmap_error(brk)))
		fail("brk", NULL, ret);

	sh.brk = brk;
	sh.ptr = brk;
	sh.end = brk;
}

void init_heap(void)
{
	heap_set(NULL);
}

void reset_heap(void)
{
	if(sh.end == sh.brk)
		return;

	heap_set(sh.brk);
}

static long align(long size)
{
	return size + (PAGE - size % PAGE) % PAGE;
}

void* heap_alloc(uint size)
{
	void* ptr = sh.ptr;
	void* end = ptr + size;
	int ret;

	if(end <= sh.end)
		goto out;

	void* brk = sh.brk;
	void* new = brk + align(end - brk);

	new = (void*)sys_brk(new);

	if((ret = mmap_error(new)))
		fail("brk", NULL, ret);
	if(end > new)
		fail("brk", NULL, 0);

	sh.end = new;
out:
	sh.ptr = end;

	return ptr;
}

void heap_reset(void* ptr)
{
	if((ptr < sh.brk) || (ptr > sh.end))
		fail("bad heap reset", NULL, 0);

	sh.ptr = ptr;
}

void output(char* buf, int len)
{
	writeall(STDOUT, buf, len);
}

void repl(char* cmsg, char* cobj, int ret)
{
	char* err = (char*)errtag;
	char* msg = (char*)cmsg;
	char* obj = (char*)cobj;

	char* eend = strpend(err);
	char* mend = strpend(msg);
	char* oend = strpend(obj);

	int elen = strelen(err, eend);
	int mlen = strelen(msg, mend);
	int olen = strelen(obj, oend);
	int size = elen + mlen + olen + 32;

	char* buf = alloca(size);
	char* p = buf;
	char* e = buf + size - 1;

	if(eend) {
		p = fmtraw(p, e, err, elen);
		p = fmtchar(p, e, ':');
	}
	if(mend) {
		p = fmtchar(p, e, ' ');
		p = fmtraw(p, e, msg, mlen);
	}
	if(oend) {
		p = fmtchar(p, e, ' ');
		p = fmtraw(p, e, obj, olen);
	}
	if(ret && (mlen || olen)) {
		p = fmtchar(p, e, ':');
	}
	if(ret) {
		p = fmtchar(p, e, ' ');
		p = fmterr(p, e, ret);
	}

	*p++ = '\n';

	output(buf, p - buf);
}
