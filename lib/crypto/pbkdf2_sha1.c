#include <string.h>
#include "sha1.h"
#include "pbkdf2.h"

/* From Wikipedia:

       F(Password, Salt, c, i) = U1 ^ U2 ^ ... ^ Uc

       U1 = PRF(Password, Salt || INT_32_BE(i))
       U2 = PRF(Password, U1)
       ...
       Uc = PRF(Password, Uc-1)

   PRF(P, I) = HMAC-SHA1(key=P, input=I)

   This should be in ../../lib/crypto/ but so far WPA2 code is the only
   place it gets used.
 
   Ref. RFC 2898 PKCS #5: Password-Based Cryptography Specification v.2 */

#define HS 20 /* SHA-1 output size, bytes */

static void F1(uint8_t* U, uint8_t* P, int Pn, uint8_t* S, int Sn, int i)
{
	int In = Sn + 4;
	char I[In];

	memcpy(I, S, Sn);

	uint8_t* q = (uint8_t*)(I + Sn);

	q[0] = (i >> 24) & 0xFF;
	q[1] = (i >> 16) & 0xFF;
	q[2] = (i >>  8) & 0xFF;
	q[3] = (i      ) & 0xFF;

	hmac_sha1(U, P, Pn, I, In);
}

static void Fc(uint8_t* U, uint8_t* P, int Pn)
{
	char* I = (char*) U;
	hmac_sha1(U, P, Pn, I, HS);
}

static void xorbuf(uint8_t* T, uint8_t* U, int n)
{
	for(int i = 0; i < n; i++)
		T[i] ^= U[i];
}

static void F(uint8_t* T, uint8_t* P, int Pn, uint8_t* S, int Sn, int c, int i)
{
	uint8_t U[HS];

	F1(U, P, Pn, S, Sn, i);
	memcpy(T, U, HS);

	for(int j = 2; j <= c; j++) {
		Fc(U, P, Pn);
		xorbuf(T, U, HS);
	}
}

void pbkdf2_sha1(uint8_t* psk, int len,
		char* pass, int passlen,
		char* salt, int saltlen, int iters)
{
	uint8_t* P = (uint8_t*) pass;  int Pn = passlen;
	uint8_t* S = (uint8_t*) salt;  int Sn = saltlen;

	int c = iters;
	int i;

	for(i = 1; HS*i <= len; i++) {
		uint8_t* T = psk + HS*(i-1);
		F(T, P, Pn, S, Sn, c, i);
	} if(i < len) {
		uint8_t T[HS];
		F(T, P, Pn, S, Sn, c, i);
		memcpy(psk + HS*(i-1), T, len - i);
	}
}
