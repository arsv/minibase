#include <sys/mman.h>
#include <sys/file.h>
#include <string.h>
#include <util.h>

#include "passblk.h"
#include "common.h"

/* Keyfile format is this:

       IV[8] TP[8] K1[32] K2[32] K3[32] ...

   IV is the initial vector for AES wrap, TP is the testpad, a pre-defined
   value used to check successful decryption, and Ki are the keys.

   AES unwrap works in-place, but we may need to ask passphrase and try
   unwrapping it more than once so we keep a copy of the keyfile in memory. */

static const char testpad[] = { 0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6 };

void load_key_data(CTX, char* name)
{
	int fd, rd;
	void* buf = ctx->keydata;
	int len = sizeof(ctx->keydata);

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((rd = sys_read(fd, buf, len)) < 0)
		fail(NULL, name, rd);
	if(rd > MAXFILE) /* too many keys */
		fail(NULL, name, -E2BIG);
	if(rd < HDRSIZE) /* too short to be valid */
		fail(NULL, name, -EINVAL);
	if((rd - HDRSIZE) % KEYSIZE) /* truncated */
		fail(NULL, name, -EINVAL);

	ctx->nkeys = (rd - HDRSIZE) / KEYSIZE;
	ctx->keysize = rd;
	ctx->keyfile = name;
}

void alloc_scrypt(CTX)
{
	int n = SCRYPT_N;
	int r = SCRYPT_R;
	int p = SCRYPT_P;
	struct scrypt* sc = &ctx->sc;

	long size = scrypt_init(sc, n, r, p);
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	void* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if(mmap_error(buf))
		fail("mmap", NULL, (long)buf);

	scrypt_temp(sc, buf, size);

	memcpy(ctx->keycopy, ctx->keydata, ctx->keysize);
}

int unwrap_keydata(CTX)
{
	byte kek[16];
	int ret;

	struct scrypt* sc = &ctx->sc;
	byte* data = ctx->keydata;
	byte* copy = ctx->keycopy;
	int size = ctx->keysize;

	void* S = data;
	int slen = SALTLEN;
	void* D = kek;
	int dlen = sizeof(kek);
	void* P = ctx->pass;
	int plen = ctx->plen;

	scrypt_data(sc, P, plen, S, slen);
	scrypt_hash(sc, D, dlen);

	void* wrapped = data + slen;
	int wraplen = size - slen;

	aes128_unwrap(kek, wrapped, wraplen);
	memzero(kek, sizeof(kek));

	if((ret = memcmp(wrapped, testpad, sizeof(testpad))))
		memcpy(data, copy, size);

	return ret;
}
