#include <string.h>

#include "sha1.h"

/* Ref. RFC 2104 HMAC: Keyed-Hashing for Message Authentication */

static void hmac_xor(uint8_t pad[64], uint8_t val)
{
	for(int i = 0; i < 64; i++)
		pad[i] ^= val;
}

static void hash_rest(struct sha1* sh, char* input, long inlen, int prev)
{
	char* ptr = input;
	char* end = input + inlen;
	int block = 64; /* = 512 bits */

	while(end - ptr >= block) {
		sha1_proc(sh, ptr);
		ptr += block;
	}

	sha1_last(sh, ptr, inlen + prev);
}

void hmac_sha1(uint8_t out[20], uint8_t* key, int klen, char* input, int inlen)
{
	uint8_t pad[64];
	struct sha1 sh;
	uint8_t hash[20];
	int hlen = sizeof(hash);

	if(klen < 0)
		klen = 0; /* wtf */
	if(klen > 64)
		klen = 64; /* TODO: key-hashing pass */

	memcpy(pad, key, klen);
	memset(pad + klen, 0, 64 - klen);
	hmac_xor(pad, 0x36);

	sha1_init(&sh);
	sha1_proc(&sh, (char*)pad);
	hash_rest(&sh, input, inlen, sizeof(pad));
	sha1_fini(&sh, hash);

	memcpy(pad, key, klen);
	memset(pad + klen, 0, 64 - klen);
	hmac_xor(pad, 0x5C);

	sha1_init(&sh);
	sha1_proc(&sh, (char*)pad);
	sha1_last(&sh, (char*)hash, 64 + hlen);
	sha1_fini(&sh, out);
}
