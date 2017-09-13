#include <crypto/aes128.h>

#include <printf.h>
#include <string.h>

#include "aesxts.h"

static void prep_tweak(uint8_t T[16], struct aes128* A, uint64_t S)
{
	for(int i = 0; i < 16; i++) {
		T[i] = S & 0xFF;
		S >>= 8;
	}

	aes128_encrypt(A, T);
}

static void next_tweak(uint8_t T[16])
{
	int i, c = 0;

	for(i = 0; i < 16; i++) {
		uint8_t ti = T[i];
		T[i] = ((ti << 1) + c);
		c = (ti >> 7) & 1;
	} if(c) {
		T[0] ^= 0x87;
	}
}

static void blk_xor(uint8_t x[16], uint8_t a[16], uint8_t b[16])
{
	uint32_t* xp = (uint32_t*) x;
	uint32_t* ap = (uint32_t*) a;
	uint32_t* bp = (uint32_t*) b;

	xp[0] = ap[0] ^ bp[0];
	xp[1] = ap[1] ^ bp[1];
	xp[2] = ap[2] ^ bp[2];
	xp[3] = ap[3] ^ bp[3];
}

typedef void (*aesfn)(struct aes128*, uint8_t*);

static void aesxts(struct aes128* A, void* rp, void* wp, uint64_t S, aesfn op)
{
	uint8_t T[16];
	uint8_t x[16];

	prep_tweak(T, A, S);
	
	for(int i = 0; i < 512; i += 16) {
		blk_xor(x, rp + i, T);
		op(A, x);
		blk_xor(wp + i, x, T);

		next_tweak(T);
	}
}

static void aesxts_encrypt(struct aes128* A, void* rp, void* wp, uint64_t S)
{
	aesxts(A, rp, wp, S, aes128_encrypt);
}

static void aesxts_decrypt(struct aes128* A, void* rp, void* wp, uint64_t S)
{
	aesxts(A, rp, wp, S, aes128_decrypt);
}

static uint8_t tmp[512];

static void test(const struct vec* v)
{
	uint8_t* K = (uint8_t*) v->key;
	uint8_t* ptx = (uint8_t*) v->ptx;
	uint8_t* ctx = (uint8_t*) v->ctx;
	uint64_t lba = v->lba;
	const char* tag = v->tag;

	struct aes128 A;

	aes128_init(&A, K);

	aesxts_encrypt(&A, ptx, tmp, lba);

	if(memcmp(tmp, ctx, 512))
		tracef("FAIL %s encrypt\n", tag);
	else
		tracef("OK %s encrypt\n", tag);

	aesxts_decrypt(&A, ctx, tmp, lba);

	if(memcmp(tmp, ptx, 512))
		tracef("FAIL %s decrypt\n", tag);
	else
		tracef("OK %s decrypt\n", tag);

	aes128_fini(&A);
}

int main(void)
{
	test(&vec20);
	test(&vec21);
	test(&vec25);

	return 0;
}
