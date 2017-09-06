#include <sys/file.h>
#include <sys/mman.h>

#include <crypto/aes128.h>
#include <crypto/scrypt.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "passblk.h"

int kflen;
char keybuf[1024];
char kplain[1024];
uint8_t kek[16];

static const char testpad[] = { 0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6 };

void load_keyfile(void)
{
	char* name = KEYFILE;

	if(kflen)
		return;

	int flags = O_RDONLY;
	struct stat st;
	int fd, ret;

	if((fd = sys_open(name, flags)) < 0)
		fail("open", name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);
	if(st.size > sizeof(keybuf))
		fail("keyfile too large", NULL, 0);

	if((ret = sys_read(fd, keybuf, st.size)) < 0)
		fail("read", name, ret);
	if(ret < st.size)
		fail("incomplete read", NULL, 0);
	if(ret < 16 || ret % 16)
		fail("invalid keyfile", name, 0);

	kflen = st.size;
}

static int scrypt(void* D, int dlen, void* P, int plen, void* S, int slen)
{
	int n = SCRYPT_N;
	int r = SCRYPT_P;
	int p = SCRYPT_P;

	struct scrypt sc;
	void* brk = sys_brk(0);
	long mem = scrypt_init(&sc, n, r, p);
	void* end = sys_brk(brk + mem);

	if(end < brk + n)
		fail("brk", NULL, ENOMEM);

	scrypt_temp(&sc, brk, end - brk);
	scrypt_data(&sc, P, plen, S, slen);
	scrypt_hash(&sc, D, dlen);

	return 0;
}

int try_passphrase(char* phrase, int phrlen)
{
	memcpy(kplain, keybuf, kflen);

	char* salt = kplain;
	int slen = 8;

	scrypt(kek, sizeof(kek), phrase, phrlen, salt, slen);
	memzero(phrase, sizeof(phrase));

	char* wrapped = kplain + slen;
	int wraplen = kflen - slen;

	aes128_unwrap(kek, wrapped, wraplen);

	int ret = memcmp(wrapped, testpad, sizeof(testpad));

	memzero(kek, sizeof(kek));
	memzero(kplain, kflen);

	return ret;
}

int check_keyindex(int ki)
{
	if(!kflen)
		load_keyfile();

	if(ki < 0 || 16*(ki + 1) > kflen)
		return -ENOKEY;

	return 0;
}

/* The index should always be correct here since all of them are passed
   through check_keyindex first. */

void* get_key(int ki)
{
	return kplain + 16*ki;
}
