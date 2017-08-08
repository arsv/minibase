#include "sha256.h"

void sha256(uint8_t out[32], char* input, long inlen)
{
	struct sha256 sh;

	sha256_init(&sh);

	char* ptr = input;
	char* end = input + inlen;
	int block = 64; /* = 512 bits */

	while(end - ptr >= block) {
		sha256_proc(&sh, ptr);
		ptr += block;
	}

	sha256_last(&sh, ptr, end - ptr, inlen);
	sha256_fini(&sh, out);
}
