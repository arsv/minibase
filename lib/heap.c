#include <sys/brk.h>
#include <fail.h>
#include <heap.h>

#define PAGE 4096

static long align(long size)
{
	return size + (PAGE - size % PAGE) % PAGE;
}

void hinit(struct heap* hp, long size)
{
	hp->brk = (void*)sysbrk(NULL);
	hp->end = (void*)sysbrk(hp->brk + align(size));
	hp->ptr = hp->brk;

	if(hp->end <= hp->brk)
		fail("cannot initialize heap", NULL, 0);
}

void* halloc(struct heap* hp, long size)
{
	void* ptr = hp->ptr;
	void* end = ptr + size;

	if(end <= hp->end)
		goto out;

	hp->end = (void*)sysbrk(hp->end + align(end - hp->end));

	if(end > hp->end)
		fail("cannot allocate memory", NULL, 0);
out:
	hp->ptr += size;

	return ptr;
}

