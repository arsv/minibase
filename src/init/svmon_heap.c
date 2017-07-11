#include <sys/brk.h>
#include <null.h>
#include "svmon.h"

/* Heap is only used for large buffers in load_dir_ents()
   and ctrl code. Custom routines because the ones from util.h
   rely on fail() atm and pid 1 should be careful with that. */

static char* brk;
static char* ptr;
static char* end;

void setup_heap(void)
{
	brk = (char*)sys_brk(NULL);
	ptr = end = brk;
}

char* heap_alloc(int len)
{
	char* old = ptr;
	char* req = old + len;

	if(req <= end)
		goto done;

	end = (char*)sys_brk(req);

	if(req > end) {
		report("out of memory", NULL, 0);
		return NULL;
	}
done:
	ptr += len;
	return old;
}

void heap_flush(void)
{
	end = ptr = brk;
}
