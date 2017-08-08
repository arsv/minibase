/* Ref. RFC 2104 HMAC: Keyed-Hashing for Message Authentication */

#include <string.h>
#include "sha256.h"

static void hmac_xor(uint8_t pad[64], uint8_t val)
{
	for(int i = 0; i < 64; i++)
		pad[i] ^= val;
}

static void hash_rest(struct sha256* sh, char* input, long inlen, int prev)
{
	char* ptr = input;
	char* end = input + inlen;
	int block = 64; /* = 512 bits */

	while(end - ptr >= block) {
		sha256_proc(sh, ptr);
		ptr += block;
	}

	sha256_last(sh, ptr, end - ptr, inlen + prev);
}

void hmac_sha256(uint8_t out[32], uint8_t* key, int klen, char* input, int inlen)
{
	uint8_t pad[64];
	struct sha256 sh;
	uint8_t hash[32];
	int hlen = sizeof(hash);

	if(klen < 0)
		klen = 0; /* wtf */
	if(klen > 64)
		klen = 64; /* TODO: key-hashing pass for longer keys */

	memcpy(pad, key, klen);
	memset(pad + klen, 0, 64 - klen);
	hmac_xor(pad, 0x36);

	sha256_init(&sh);
	sha256_proc(&sh, (char*)pad);
	hash_rest(&sh, input, inlen, sizeof(pad));
	sha256_fini(&sh, hash);

	memcpy(pad, key, klen);
	memset(pad + klen, 0, 64 - klen);
	hmac_xor(pad, 0x5C);

	sha256_init(&sh);
	sha256_proc(&sh, (char*)pad);
	sha256_last(&sh, (char*)hash, hlen, 64 + hlen);
	sha256_fini(&sh, out);
}
