#include <bits/ints.h>

struct aes128 {
	uint32_t W[44];  /* 11 round keys, 4x4 each */
};

void aes128_init(struct aes128* ctx, uint8_t key[16]);
void aes128_decrypt(struct aes128* ctx, uint8_t blk[16]);
void aes128_fini(struct aes128* ctx);

void aes128_unwrap(uint8_t key[16], void* buf, int len);
