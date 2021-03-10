#include <string.h>
#include <nlusctl.h>
#include <cdefs.h>

void uc_put_bin(struct ucbuf* uc, int key, void* src, int len)
{
	struct ucattr* at;
	int alloc = ((len + 3) & ~3);

	if(!(at = uc_put(uc, key, len, alloc)))
		return;

	void* dst = at->payload;

	if(alloc > len) { /* padding was needed */
		int off = len & ~3;
		int* tail = dst + off;
		/* initialize padding to appease valgrind */
		*tail = 0;
	}

	memcpy(dst, src, len);
}

void uc_put_str(struct ucbuf* uc, int key, char* str)
{
	uc_put_bin(uc, key, str, strnlen(str, 1024) + 1);
}
