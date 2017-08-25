#include <sys/mman.h>
#include <heap.h>
#include <util.h>

static long align(long size)
{
	return size + (PAGE - size % PAGE) % PAGE;
}

void hinit(struct heap* hp, long size)
{
	hp->brk = (void*)sys_brk(NULL);
	hp->end = (void*)sys_brk(hp->brk + align(size));
	hp->ptr = hp->brk;

	if(hp->end <= hp->brk)
		fail("cannot initialize heap", NULL, 0);
}

void hextend(struct heap* hp, long size)
{
	void* ptr = hp->ptr;
	void* end = ptr + size;

	if(end <= hp->end)
		return;

	hp->end = (void*)sys_brk(hp->end + align(end - hp->end));

	if(end > hp->end)
		fail("cannot allocate memory", NULL, 0);
}

void* halloc(struct heap* hp, long size)
{
	void* ptr = hp->ptr;

	hextend(hp, size);

	hp->ptr += size;

	return ptr;
}

