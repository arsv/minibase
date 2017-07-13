#include <sys/brk.h>
#include <null.h>
#include "svmon.h"

/* Heap is only used for large buffers in load_dir_ents()
   and ctrl code. Custom routines because the ones from util.h
   rely on fail() atm and pid 1 should be careful with that. */

static void* brk;
static void* ptr;
static void* end;

void setup_heap(void)
{
	brk = (void*)sys_brk(NULL);
	ptr = end = brk;
}

void* heap_alloc(int len)
{
	void* old = ptr;
	void* new = ptr + len;

	if(new <= end)
		goto done;

	int aligned = len + (PAGE - len) % PAGE;
	void* req = old + aligned;

	end = (char*)sys_brk(req);

	if(new > end) {
		report("out of memory", NULL, 0);
		return NULL;
	}
done:
	ptr = new;
	return old;
}

void heap_trim(void* old)
{
	if(old < brk || old > end)
		return;

	ptr = old;
	gg.heapreq = 1;
}

void heap_flush(void)
{
	gg.heapreq = 0;

	if(ptr > brk)
		report("retaining non-empty heap", NULL, 0);
	else if(end == brk)
		return;
	else
		end = (void*)sys_brk(brk);
}
