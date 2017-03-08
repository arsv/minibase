#include <bits/ints.h>

struct sha1 {
	uint32_t H[5];
	uint32_t W[80];
};

/* contiguous input */
void sha1(uint8_t out[20], char* input, long inlen);

/* multi-block input */
void sha1_init(struct sha1* sh);
void sha1_proc(struct sha1* sh, char blk[64]);
void sha1_last(struct sha1* sh, char* ptr, uint64_t len);
void sha1_fini(struct sha1* sh, uint8_t out[20]);

/* HMAC, contiguous only */
void hmac_sha1(uint8_t out[20], uint8_t* key, int klen, char* input, int inlen);
