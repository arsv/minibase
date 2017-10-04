/* Ref. RFC 3394 Advanced Encryption Standard (AES) Key Wrap Algorithm */

#include <string.h>
#include <endian.h>
#include "aes128.h"

static uint64_t wrapmask(int n)
{
#ifdef BIGENDIAN
	return n;
#else
	return ((uint64_t)htonl(n) << 32);
#endif
}

void aes128_wrap(byte key[16], void* buf, ulong len)
{
	struct aes128 ae;

	ulong n = len / 8 - 1;
	uint64_t* R = buf;
	byte B[16];
	uint i, j;

	aes128_init(&ae, key);

	for(j = 0; j <= 5; j++)
		for(i = 1; i <= n; i++) {
			memcpy(B + 0, &R[0], 8);
			memcpy(B + 8, &R[i], 8);

			aes128_encrypt(&ae, B);

			memcpy(&R[0], B + 0, 8);
			memcpy(&R[i], B + 8, 8);

			R[0] ^= wrapmask(n*j + i);
		}

	aes128_fini(&ae);
}
