/* Ref. RFC 6234 US Secure Hash Algorithms (SHA and SHA-based HMAC and HKDF).

   Heavily modified, but still based on, the reference implementation from
   the RFC. Original copyright notice follows: */

/* Copyright (c) 2011 IETF Trust and the persons identified as authors
   of the code. All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

     * Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

     * Neither the name of Internet Society, IETF or IETF Trust, nor the names
       of specific contributors, may be used to endorse or promote products
       derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE. */

#include <bits/types.h>

#include <endian.h>
#include <string.h>
#include <format.h>

#include "sha256.h"

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t rotr(uint32_t x, int n)
{
	return (x >> n) | (x << (32 - n));
}

static uint32_t shr(uint32_t x, int n)
{
	return (x >> n);
}

void sha256_init(struct sha256* sh)
{
	uint32_t* H = sh->H;

	H[0] = 0x6a09e667;
	H[1] = 0xbb67ae85;
	H[2] = 0x3c6ef372;
	H[3] = 0xa54ff53a;
	H[4] = 0x510e527f;
	H[5] = 0x9b05688c;
	H[6] = 0x1f83d9ab;
	H[7] = 0x5be0cd19;
}

static uint32_t ch(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) ^ ((~x) & z);
}

static uint32_t maj(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t bsig0(uint32_t x)
{
	return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static uint32_t bsig1(uint32_t x)
{
	return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static uint32_t ssig0(uint32_t x)
{
	return rotr(x, 7) ^ rotr(x, 18) ^ shr(x, 3);
}

static uint32_t ssig1(uint32_t x)
{
	return rotr(x, 17) ^ rotr(x, 19) ^ shr(x, 10);
}

static void sha256_hash(struct sha256* sh)
{
	uint32_t* H = sh->H;
	uint32_t* W = sh->W;
	uint32_t t1, t2;
	int i;

	uint32_t a = H[0], b = H[1], c = H[2], d = H[3],
	         e = H[4], f = H[5], g = H[6], h = H[7];

	for(i = 0; i < 64; i++) {
		t1 = h + bsig1(e) + ch(e,f,g) + K[i] + W[i];
		t2 = bsig0(a) + maj(a,b,c);

		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	H[0] += a; H[1] += b; H[2] += c; H[3] += d;
	H[4] += e; H[5] += f; H[6] += g; H[7] += h;
}

void sha256_fini(struct sha256* sh, uint8_t out[32])
{
	uint32_t* po = (uint32_t*) out;

	for(int i = 0; i < 8; i++)
		po[i] = htonl(sh->H[i]);
}

static void sha256_load(struct sha256* sh, char blk[64])
{
	int i;
	uint32_t* W = sh->W;

	for(i = 0; i < 16; i++)
		W[i] = ntohl(*((uint32_t*)(blk + 4*i)));
	for(i = 16; i < 64; i++)
		W[i] = ssig1(W[i-2]) + W[i-7] + ssig0(W[i-15]) + W[i-16];
}

/* Size and padding matches that of SHA-1; see comments there. */

static void sha256_put_size(char blk[64], uint64_t total)
{
	uint32_t* hw = (uint32_t*)(blk + 64 - 8);
	uint32_t* lw = (uint32_t*)(blk + 64 - 4);

	uint64_t bits = total << 3;
	*hw = htonl(bits >> 32);
	*lw = htonl(bits & 0xFFFFFFFF);
}

static void sha256_load_pad1(struct sha256* sh, char* ptr, int tail, uint64_t total)
{
	char block[64];

	memcpy(block, ptr, tail);
	memset(block + tail, 0, 64 - tail);

	block[tail] = 0x80;
	sha256_put_size(block, total);

	sha256_load(sh, block);
}

static void sha256_load_pad2(struct sha256* sh, char* ptr, int tail)
{
	char block[64];

	memcpy(block, ptr, tail);
	memset(block + tail, 0, 64 - tail);

	block[tail] = 0x80;

	sha256_load(sh, block);
}

static void sha256_load_pad0(struct sha256* sh, uint64_t total)
{
	char block[64];

	memset(block, 0, 64);

	sha256_put_size(block, total);
	sha256_load(sh, block);
}

void sha256_proc(struct sha256* sh, char blk[64])
{
	sha256_load(sh, blk);
	sha256_hash(sh);
}

void sha256_last(struct sha256* sh, char* ptr, int len, uint64_t total)
{
	int tail = len % 64;

	if(tail > 55) {
		sha256_load_pad2(sh, ptr, tail);
		sha256_hash(sh);
		sha256_load_pad0(sh, total);
		sha256_hash(sh);
	} else {
		sha256_load_pad1(sh, ptr, tail, total);
		sha256_hash(sh);
	}
}
