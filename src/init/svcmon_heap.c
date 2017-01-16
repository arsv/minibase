#include <sys/brk.h>
#include <null.h>

#include "svcmon.h"

static char* brk;
static char* ptr;
static char* end;

void setbrk(void)
{
	brk = (char*)sysbrk(NULL);
	ptr = end = brk;
}

char* alloc(int len)
{
	char* old = ptr;
	char* req = old + len;

	if(req <= end)
		goto done;

	end = (char*)sysbrk(req);

	if(req > end) {
		report("out of memory", NULL, 0);
		return NULL;
	}
done:
	ptr += len;
	return old;
}

void afree(void)
{
	end = ptr = (char*)sysbrk(brk);
}
