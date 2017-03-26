#include <bits/ints.h>
#include <endian.h>
#include <string.h>
#include <format.h>

#include "sha1.h"

/* Ref. RFC 3174 US Secure Hash Algorithm 1 */

static uint32_t rol(uint32_t x, int n)
{
	return (x << n) | (x >> (32 - n));
}

static uint32_t fa(uint32_t b, uint32_t c, uint32_t d)
{
	return (b & c) | (~b & d);
}

static uint32_t fb(uint32_t b, uint32_t c, uint32_t d)
{
	return b ^ c ^ d;
}

static uint32_t fc(uint32_t b, uint32_t c, uint32_t d)
{
	return (b & c) | (b & d) | (c & d);
}

void sha1_init(struct sha1* sh)
{
	uint32_t* H = sh->H;

	H[0] = 0x67452301;
	H[1] = 0xEFCDAB89;
	H[2] = 0x98BADCFE;
	H[3] = 0x10325476;
	H[4] = 0xC3D2E1F0;
}

/* The four rounds are exactly the same except for fx() variants
   and the constants at the end. Unrolled to avoid possible
   indirection penalties. */

static void sha1_hash(struct sha1* sh)
{
	uint32_t* H = sh->H;
	uint32_t* W = sh->W;

	uint32_t A = H[0];
	uint32_t B = H[1];
	uint32_t C = H[2];
	uint32_t D = H[3];
	uint32_t E = H[4];

	int i, j = 0;
	uint32_t temp;

	for(i = 0; i < 20; i++, j++) {
		temp = rol(A, 5) + fa(B, C, D) + E + W[j] + 0x5A827999;
		E = D; D = C; C = rol(B, 30); B = A; A = temp;
	}
	for(i = 0; i < 20; i++, j++) {
		temp = rol(A, 5) + fb(B, C, D) + E + W[j] + 0x6ED9EBA1;
		E = D; D = C; C = rol(B, 30); B = A; A = temp;
	}
	for(i = 0; i < 20; i++, j++) {
		temp = rol(A, 5) + fc(B, C, D) + E + W[j] + 0x8F1BBCDC;
		E = D; D = C; C = rol(B, 30); B = A; A = temp;
	}
	for(i = 0; i < 20; i++, j++) {
		temp = rol(A, 5) + fb(B, C, D) + E + W[j] + 0xCA62C1D6;
		E = D; D = C; C = rol(B, 30); B = A; A = temp;
	}

	H[0] += A;
	H[1] += B;
	H[2] += C;
	H[3] += D;
	H[4] += E;
}

void sha1_fini(struct sha1* sh, uint8_t out[20])
{
	uint32_t* po = (uint32_t*) out;

	for(int i = 0; i < 5; i++)
		po[i] = htonl(sh->H[i]);
}

static void sha1_load(struct sha1* sh, char blk[64])
{
	int i;
	uint32_t* W = sh->W;

	for(i = 0; i < 16; i++)
		W[i] = ntohl(*((uint32_t*)(blk + 4*i)));

	for(i = 16; i < 80; i++)
		W[i] = rol(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1);
}

/* Padding: append one bit (0x80), add zeroes, then put 64-bit
   total message length at the end of the 64-byte long block.

   In case there's no space in the last block for 1+8 pad bytes,
   the size gets pushed into an empty block; that's pad2+pad0.
   When the last block is less that 55 bytes, both 0x80 and the
   size fit there; that's pad1.

   Stored size is in *bits* not bytes! */

static void sha1_put_size(char blk[64], uint64_t total)
{
	uint32_t* hw = (uint32_t*)(blk + 64 - 8);
	uint32_t* lw = (uint32_t*)(blk + 64 - 4);

	uint64_t bits = total << 3;
	*hw = htonl(bits >> 32);
	*lw = htonl(bits & 0xFFFFFFFF);
}

static void sha1_load_pad1(struct sha1* sh, char* ptr, int tail, uint64_t total)
{
	char block[64];

	memcpy(block, ptr, tail);
	memset(block + tail, 0, 64 - tail);

	block[tail] = 0x80;
	sha1_put_size(block, total);

	sha1_load(sh, block);
}

static void sha1_load_pad2(struct sha1* sh, char* ptr, int tail)
{
	char block[64];

	memcpy(block, ptr, tail);
	memset(block + tail, 0, 64 - tail);

	block[tail] = 0x80;

	sha1_load(sh, block);
}

static void sha1_load_pad0(struct sha1* sh, char* ptr, uint64_t total)
{
	char block[64];

	memset(block, 0, 64);

	sha1_put_size(block, total);
	sha1_load(sh, block);
}

/* Input must be processed in blocks of 64 bytes, except for the last
   block which may be truncated and which will get padded anyway.

   SHA1 needs *complete* size of the input, including all regular blocks
   processes from the very start, at the end of padding. The data pointed
   to by ptr must be (total % 64) bytes long. */

void sha1_proc(struct sha1* sh, char blk[64])
{
	sha1_load(sh, blk);
	sha1_hash(sh);
}

void sha1_last(struct sha1* sh, char* ptr, int len, uint64_t total)
{
	int tail = len % 64;

	if(tail > 55) {
		sha1_load_pad2(sh, ptr, tail);
		sha1_hash(sh);
		sha1_load_pad0(sh, ptr, total);
		sha1_hash(sh);
	} else {
		sha1_load_pad1(sh, ptr, tail, total);
		sha1_hash(sh);
	}
}
