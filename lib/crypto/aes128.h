#include <bits/types.h>

struct aes128 {
	uint32_t W[44];  /* 11 round keys, 4x4 each */
	uint32_t S[4];   /* state vector */
};

void aes128_init(struct aes128* ctx, const byte key[16]);
void aes128_decrypt(struct aes128* ctx, byte blk[16]);
void aes128_encrypt(struct aes128* ctx, byte blk[16]);
void aes128_fini(struct aes128* ctx);

void aes128_wrap(byte key[16], void* buf, ulong len);
void aes128_unwrap(byte key[16], void* buf, ulong len);
