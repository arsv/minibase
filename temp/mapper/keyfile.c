#include <crypto/aes128.h>
#include <crypto/scrypt.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/brk.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <fail.h>

#include "keyfile.h"

static const char testpad[] = { 0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6 };

static int scrypt(void* D, int dlen, void* P, int plen, void* S, int slen)
{
	int n = 8192;
	int r = 1;
	int p = 1;

	struct scrypt sc;
	void* brk = (void*)sys_brk(0);
	long mem = scrypt_init(&sc, n, r, p);
	void* end = (void*)sys_brk(brk + mem);

	if(end < brk + n)
		fail("brk", NULL, ENOMEM);

	scrypt_temp(&sc, brk, end - brk);
	scrypt_data(&sc, P, plen, S, slen);
	scrypt_hash(&sc, D, dlen);

	return 0;
}

void hash_passphrase(struct keyfile* kf, char* phrase, int phrlen)
{
	void* kek = kf->kek;
	int klen = sizeof(kf->kek);
	void* salt = kf->salt;
	int slen = sizeof(kf->salt);

	scrypt(kek, klen, phrase, phrlen, salt, slen);
}

void unwrap_keyfile(struct keyfile* kf, char* phrase, int phrlen)
{
	hash_passphrase(kf, phrase, phrlen);
	memzero(phrase, sizeof(phrase));

	int slen = sizeof(kf->salt);
	void* wrapped = kf->wrapped;
	int wraplen = kf->len - slen;

	aes128_unwrap(kf->kek, wrapped, wraplen);

	if(memcmp(wrapped, testpad, sizeof(testpad)))
		fail("incorrect passphrase", NULL, 0);
}

void copy_valid_iv(struct keyfile* kf)
{
	memcpy(kf->iv, testpad, sizeof(testpad));
}

void read_keyfile(struct keyfile* kf, char* name)
{
	int flags = O_RDONLY;
	struct stat st;
	int fd, ret;

	if((fd = sys_open(name, flags)) < 0)
		fail("open", name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);
	if(st.size > sizeof(kf->buf))
		fail("keyfile too large", NULL, 0);

	if((ret = sys_read(fd, kf->buf, st.size)) < 0)
		fail("read", name, ret);
	if(ret < st.size)
		fail("incomplete read", NULL, 0);
	if(ret < 16 || ret % 16)
		fail("invalid keyfile", name, 0);

	kf->len = st.size;
}

void write_keyfile(struct keyfile* kf, char* name, int flags)
{
	int wr, fd;
	int slen = 8;

	aes128_wrap(kf->kek, kf->buf + slen, kf->len - slen);

	if((fd = sys_open3(name, O_WRONLY | flags, 0600)) < 0)
		fail(NULL, name, fd);

	if((wr = writeall(fd, kf->buf, kf->len)) < 0)
		warn("write", name, wr);

	sys_close(fd);
	memzero(kf->buf, sizeof(kf->buf));
}
