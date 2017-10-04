/* Based on Colin Percival's original code. Modified to fit the minibase style.
   In this version the caller is responsible for allocating memory.
   Original copyright notice follows: */
/*-
 * Copyright 2009 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

#include <bits/ints.h>
#include <bits/errno.h>
#include <crypto/pbkdf2.h>
#include <endian.h>
#include <string.h>

#include "scrypt.h"

static void blkcpy(uint32_t* dst, const uint32_t* src, size_t n)
{
	for(size_t i = 0; i < n; i++)
		dst[i] = src[i];
}

static void blkxor(uint32_t* dst, const uint32_t* src, size_t n)
{
	for(size_t i = 0; i < n; i++)
		dst[i] ^= src[i];
}

static void blkadd(uint32_t* dst, uint32_t* src, size_t n)
{
	for(size_t i = 0; i < n; i++)
		dst[i] += src[i];
}

static uint32_t rotl(uint32_t x, int n)
{
	return (x << n) | (x >> (32 - n));
}

static void sx(uint32_t* x, int k, int i, int j, int n)
{
	x[k] ^= rotl(x[i] + x[j], n);
}

static void salsa20_8(uint32_t B[16])
{
	uint32_t x[16];
	size_t i;

	blkcpy(x, B, 16);

	for (i = 0; i < 8; i += 2) {
		sx(x, 4, 0,12, 7);  sx(x, 8, 4, 0, 9);
		sx(x,12, 8, 4,13);  sx(x, 0,12, 8,18);
		sx(x, 9, 5, 1, 7);  sx(x,13, 9, 5, 9);
		sx(x, 1,13, 9,13);  sx(x, 5, 1,13,18);
		sx(x,14,10, 6, 7);  sx(x, 2,14,10, 9);
		sx(x, 6, 2,14,13);  sx(x,10, 6, 2,18);
		sx(x, 3,15,11, 7);  sx(x, 7, 3,15, 9);
		sx(x,11, 7, 3,13);  sx(x,15,11, 7,18);

		sx(x, 1, 0, 3, 7);  sx(x, 2, 1, 0, 9);
		sx(x, 3, 2, 1,13);  sx(x, 0, 3, 2,18);
		sx(x, 6, 5, 4, 7);  sx(x, 7, 6, 5, 9);
		sx(x, 4, 7, 6,13);  sx(x, 5, 4, 7,18);
		sx(x,11,10, 9, 7);  sx(x, 8,11,10, 9);
		sx(x, 9, 8,11,13);  sx(x,10, 9, 8,18);
		sx(x,12,15,14, 7);  sx(x,13,12,15, 9);
		sx(x,14,13,12,13);  sx(x,15,14,13,18);
	}

	blkadd(B, x, 16);
}

static void blockmix(const uint32_t* Bin, uint32_t* Bout, uint32_t* X, size_t r)
{
	size_t i;

	blkcpy(X, &Bin[(2*r - 1)*16], 16);

	for (i = 0; i < 2*r; i += 2) {
		blkxor(X, &Bin[i*16], 16);
		salsa20_8(X);
		blkcpy(&Bout[i*8], X, 16);
		blkxor(X, &Bin[i*16 + 16], 16);
		salsa20_8(X);
		blkcpy(&Bout[i*8 + r*16], X, 16);
	}
}

static uint64_t integerify(uint32_t* B, size_t r)
{
	size_t i = (2*r - 1)*16;
	uint64_t rh = B[i + 1];
	uint64_t rl = B[i + 0];
	return ((rh << 32) | rl);
}

static void salsamix(uint32_t* B, int r, int N, uint32_t* V, void* XY)
{
	uint32_t* X = XY;
	uint32_t* Y = (XY + 128 * r);
	uint32_t* Z = (XY + 256 * r);

	int i, j;
	long k;

	for (k = 0; k < 32*r; k++)
		X[k] = itohl(B[k]);

	for (i = 0; i < N; i += 2) {
		blkcpy(&V[i*(32*r)], X, 32*r);
		blockmix(X, Y, Z, r);
		blkcpy(&V[(i+1)*(32*r)], Y, 32*r);
		blockmix(Y, X, Z, r);
	}

	for (i = 0; i < N; i += 2) {
		j = integerify(X, r) & (N - 1);
		blkxor(X, &V[j*(32*r)], 32*r);
		blockmix(X, Y, Z, r);
		j = integerify(Y, r) & (N - 1);
		blkxor(Y, &V[j*(32*r)], 32*r);
		blockmix(Y, X, Z, r);
	}

	for (k = 0; k < 32*r; k++)
		B[k] = htoil(X[k]);
}

static void spbkdf(struct scrypt* sc, void* salt, int slen, void* dk, int dklen)
{
	char* pass = sc->pass;
	int plen = sc->passlen;

	pbkdf2_sha256(dk, dklen, pass, plen, salt, slen, 1);
}

ulong scrypt_init(struct scrypt* sc, uint n, uint r, uint p)
{
	memzero(sc, sizeof(*sc));

	sc->n = n;
	sc->p = p;
	sc->r = r;

	ulong B0size = 128*r*p;
	ulong XYsize = 256*r + 64;
	ulong V0size = 128*r*n;
	ulong need = B0size + XYsize + V0size; 

	sc->templen = need;

	return need;
}

int scrypt_temp(struct scrypt* sc, void* buf, ulong len)
{
	sc->temp = buf;

	if(len < sc->templen)
		return -ENOBUFS;

	sc->templen = len;

	return 0;
}

int scrypt_data(struct scrypt* sc, void* P, uint plen, void* S, uint slen)
{
	sc->pass = P;
	sc->passlen = plen;

	sc->salt = S;
	sc->saltlen = slen;

	return 0;
}

void scrypt_hash(struct scrypt* sc, void* dk, uint dklen)
{
	int r = sc->r;
	int p = sc->p;
	int n = sc->n;
	int i;

	ulong B0size = 128*r*p;
	ulong XYsize = 256*r + 64;

	uint32_t* B = (sc->temp + 0);
	uint32_t* XY = (sc->temp + B0size);
	uint32_t* V = (sc->temp + B0size + XYsize);

	spbkdf(sc, sc->salt, sc->saltlen, B, 4*p*32*r);

	for(i = 0; i < p; i++)
		salsamix(&B[i*32*r], r, n, V, XY);

	spbkdf(sc, B, 4*p*32*r, dk, dklen);
}
