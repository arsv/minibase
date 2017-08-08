#include <bits/ints.h>
#include <bits/errno.h>
#include <crypto/pbkdf2.h>
#include <endian.h>
#include <string.h>

#include "scrypt.h"

static uint32_t le32dec(const void* pp)
{
	const uint8_t* p = (uint8_t const*)pp;

	return ((uint32_t)(p[0]) + ((uint32_t)(p[1]) << 8) +
	    ((uint32_t)(p[2]) << 16) + ((uint32_t)(p[3]) << 24));
}

static void le32enc(void* pp, uint32_t x)
{
	uint8_t * p = (uint8_t *)pp;

	p[0] = x & 0xff;
	p[1] = (x >> 8) & 0xff;
	p[2] = (x >> 16) & 0xff;
	p[3] = (x >> 24) & 0xff;
}

static void blkcpy(void* dest, const void* src, size_t len)
{
	size_t* D = dest;
	const size_t* S = src;
	size_t L = len / sizeof(size_t);
	size_t i;

	for(i = 0; i < L; i++)
		D[i] = S[i];
}

static void blkxor(void* dest, const void* src, size_t len)
{
	size_t* D = dest;
	const size_t* S = src;
	size_t L = len / sizeof(size_t);
	size_t i;

	for (i = 0; i < L; i++)
		D[i] ^= S[i];
}

static void salsa20_8(uint32_t B[16])
{
	uint32_t x[16];
	size_t i;

	blkcpy(x, B, 64);
	for (i = 0; i < 8; i += 2) {
#define R(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
		/* Operate on columns. */
		x[ 4] ^= R(x[ 0]+x[12], 7);  x[ 8] ^= R(x[ 4]+x[ 0], 9);
		x[12] ^= R(x[ 8]+x[ 4],13);  x[ 0] ^= R(x[12]+x[ 8],18);

		x[ 9] ^= R(x[ 5]+x[ 1], 7);  x[13] ^= R(x[ 9]+x[ 5], 9);
		x[ 1] ^= R(x[13]+x[ 9],13);  x[ 5] ^= R(x[ 1]+x[13],18);

		x[14] ^= R(x[10]+x[ 6], 7);  x[ 2] ^= R(x[14]+x[10], 9);
		x[ 6] ^= R(x[ 2]+x[14],13);  x[10] ^= R(x[ 6]+x[ 2],18);

		x[ 3] ^= R(x[15]+x[11], 7);  x[ 7] ^= R(x[ 3]+x[15], 9);
		x[11] ^= R(x[ 7]+x[ 3],13);  x[15] ^= R(x[11]+x[ 7],18);

		/* Operate on rows. */
		x[ 1] ^= R(x[ 0]+x[ 3], 7);  x[ 2] ^= R(x[ 1]+x[ 0], 9);
		x[ 3] ^= R(x[ 2]+x[ 1],13);  x[ 0] ^= R(x[ 3]+x[ 2],18);

		x[ 6] ^= R(x[ 5]+x[ 4], 7);  x[ 7] ^= R(x[ 6]+x[ 5], 9);
		x[ 4] ^= R(x[ 7]+x[ 6],13);  x[ 5] ^= R(x[ 4]+x[ 7],18);

		x[11] ^= R(x[10]+x[ 9], 7);  x[ 8] ^= R(x[11]+x[10], 9);
		x[ 9] ^= R(x[ 8]+x[11],13);  x[10] ^= R(x[ 9]+x[ 8],18);

		x[12] ^= R(x[15]+x[14], 7);  x[13] ^= R(x[12]+x[15], 9);
		x[14] ^= R(x[13]+x[12],13);  x[15] ^= R(x[14]+x[13],18);
#undef R
	}
	for (i = 0; i < 16; i++)
		B[i] += x[i];
}

static void blockmix(const uint32_t* Bin, uint32_t* Bout, uint32_t* X, size_t r)
{
	size_t i;

	/* 1: X <-- B_{2r - 1} */
	blkcpy(X, &Bin[(2 * r - 1) * 16], 64);

	/* 2: for i = 0 to 2r - 1 do */
	for (i = 0; i < 2 * r; i += 2) {
		/* 3: X <-- H(X \xor B_i) */
		blkxor(X, &Bin[i * 16], 64);
		salsa20_8(X);
		/* 4: Y_i <-- X */
		/* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
		blkcpy(&Bout[i * 8], X, 64);
		/* 3: X <-- H(X \xor B_i) */
		blkxor(X, &Bin[i * 16 + 16], 64);
		salsa20_8(X);
		/* 4: Y_i <-- X */
		/* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
		blkcpy(&Bout[i * 8 + r * 16], X, 64);
	}
}

static uint64_t integerify(const void * B, size_t r)
{
	const uint32_t * X = (const void *)(B + (2 * r - 1) * 64);

	return (((uint64_t)(X[1]) << 32) + X[0]);
}

static void salsamix(uint8_t* B, int r, int N, uint32_t* V, void* XY)
{
	uint32_t* X = XY;
	uint32_t* Y = (XY + 128 * r);
	uint32_t* Z = (XY + 256 * r);

	int i;
	int j;
	long k;

	/* 1: X <-- B */
	for (k = 0; k < 32*r; k++)
		X[k] = le32dec(&B[4*k]);

	/* 2: for i = 0 to N - 1 do */
	for (i = 0; i < N; i += 2) {
		/* 3: V_i <-- X */
		blkcpy(&V[i*(32*r)], X, 128*r);
		/* 4: X <-- H(X) */
		blockmix(X, Y, Z, r);
		/* 3: V_i <-- X */
		blkcpy(&V[(i+1)*(32*r)], Y, 128*r);
		/* 4: X <-- H(X) */
		blockmix(Y, X, Z, r);
	}

	/* 6: for i = 0 to N - 1 do */
	for (i = 0; i < N; i += 2) {
		/* 7: j <-- Integerify(X) mod N */
		j = integerify(X, r) & (N - 1);
		/* 8: X <-- H(X \xor V_j) */
		blkxor(X, &V[j*(32*r)], 128*r);
		blockmix(X, Y, Z, r);
		/* 7: j <-- Integerify(X) mod N */
		j = integerify(Y, r) & (N - 1);
		/* 8: X <-- H(X \xor V_j) */
		blkxor(Y, &V[j*(32*r)], 128*r);
		blockmix(Y, X, Z, r);
	}

	/* 10: B' <-- X */
	for (k = 0; k < 32*r; k++)
		le32enc(&B[4*k], X[k]);
}

static void pbkdf(struct scrypt* sc, void* salt, int slen, void* dk, int dklen)
{
	char* pass = sc->pass;
	int plen = sc->passlen;

	pbkdf2_sha256(dk, dklen, pass, plen, salt, slen, 1);
}

long scrypt_init(struct scrypt* sc, int n, int r, int p)
{
	memzero(sc, sizeof(*sc));

	sc->n = n;
	sc->p = p;
	sc->r = r;

	size_t B0size = 128*r*p;
	size_t XYsize = 256*r + 64;
	size_t V0size = 128*r*n;
	long need = B0size + XYsize + V0size; 

	sc->templen = need;

	return need;
}

int scrypt_temp(struct scrypt* sc, void* buf, long len)
{
	sc->temp = buf;

	if(len < sc->templen)
		return -ENOBUFS;

	sc->templen = len;

	return 0;
}

int scrypt_data(struct scrypt* sc, void* P, int plen, void* S, int slen)
{
	sc->pass = P;
	sc->passlen = plen;

	sc->salt = S;
	sc->saltlen = slen;

	return 0;
}

void scrypt_hash(struct scrypt* sc, void* dk, int dklen)
{
	int r = sc->r;
	int p = sc->p;
	int n = sc->n;
	int i;

	size_t B0size = 128*r*p;
	size_t XYsize = 256*r + 64;

	uint8_t* B = (sc->temp + 0);
	uint32_t* XY = (sc->temp + B0size);
	uint32_t* V = (sc->temp + B0size + XYsize);

	pbkdf(sc, sc->salt, sc->saltlen, B, p*128*r);

	for(i = 0; i < p; i++)
		salsamix(&B[i*128*r], r, n, V, XY);

	pbkdf(sc, B, p*128*r, dk, dklen);
}
