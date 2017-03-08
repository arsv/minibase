#include "sha1.h"

void sha1(uint8_t out[20], char* input, long inlen)
{
	struct sha1 sh;

	sha1_init(&sh);

	char* ptr = input;
	char* end = input + inlen;
	int block = 64; /* = 512 bits */

	while(end - ptr >= block) {
		sha1_proc(&sh, ptr);
		ptr += block;
	}

	sha1_last(&sh, ptr, inlen);
	sha1_fini(&sh, out);
}
