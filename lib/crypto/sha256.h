#include <bits/types.h>

struct sha256 {
	uint32_t H[8];
	uint32_t W[64];
};

/* privitives for multi-block input */
void sha256_init(struct sha256* sh);
void sha256_proc(struct sha256* sh, char blk[64]);
void sha256_last(struct sha256* sh, char* ptr, int len, uint64_t total);
void sha256_fini(struct sha256* sh, uint8_t out[32]);

/* contiguous input */
void sha256(uint8_t out[32], char* input, long inlen);

/* HMAC, contiguous only */
void hmac_sha256(uint8_t out[32], uint8_t* key, int klen, char* input, int inlen);
