#include <string.h>
#include <nlusctl.h>
#include <cdefs.h>

void uc_put_strn(struct ucbuf* uc, int key, char* src, int max)
{
	struct ucattr* at;
	int len = strnlen(src, max);
	int lenz = len + 1;
	int alloc = ((lenz + 3) & ~3);

	if(!(at = uc_put(uc, key, lenz, alloc)))
		return;

	void* dst = at->payload;

	if(alloc > len) { /* padding was needed */
		int off = len & ~3;
		int* tail = dst + off;
		/* initialize padding to appease valgrind */
		*tail = 0;
	}

	memcpy(dst, src, len);

	char* end = dst + len;

	*end = '\0';
}
