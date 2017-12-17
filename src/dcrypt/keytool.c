#include <bits/ioctl/tty.h>
#include <crypto/aes128.h>
#include <crypto/scrypt.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "keytool.h"

static const char testpad[] = { 0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6 };

static void clear_echo(struct termios* to, int* flag)
{
	struct termios ts;

	if(sys_ioctl(STDIN, TCGETS, &ts) < 0)
		return;

	memcpy(to, &ts, sizeof(ts));

	ts.lflag &= ~ECHO;
	ts.lflag |= ECHONL;

	if(sys_ioctl(STDIN, TCSETS, &ts) < 0)
		return;

	*flag = 1;
}

static void reset_echo(struct termios* ts, int* flag)
{
	if(!*flag) return;

	sys_ioctl(STDIN, TCSETS, ts);
}

int ask(char* tag, char* buf, int len)
{
	int rd, noecho;
	struct termios ts;

	sys_write(STDOUT, tag, strlen(tag));

	clear_echo(&ts, &noecho);
	rd = sys_read(STDIN, buf, len);
	reset_echo(&ts, &noecho);

	if(rd < 0)
		fail("read", "stdin", rd);
	if(rd >= len)
		fail("passphrase too long", NULL, 0);

	if(rd && buf[rd-1] == '\n')
		rd--;
	if(rd == 0)
		fail("empty passphrase", NULL, 0);

	buf[rd] = '\0';

	return rd;
}

static int scrypt(void* D, int dlen, void* P, int plen, void* S, int slen)
{
	int n = SCRYPT_N;
	int r = SCRYPT_P;
	int p = SCRYPT_P;

	struct scrypt sc;
	long size = scrypt_init(&sc, n, r, p);

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	void* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if(mmap_error(buf))
		fail("mmap", NULL, (long)buf);

	scrypt_temp(&sc, buf, size);
	scrypt_data(&sc, P, plen, S, slen);
	scrypt_hash(&sc, D, dlen);

	return 0;
}

void hash_passphrase(struct keyfile* kf, char* phrase, int phrlen)
{
	void* kek = kf->kek;
	int klen = sizeof(kf->kek);
	void* salt = kf->buf;
	int slen = SALTLEN;

	scrypt(kek, klen, phrase, phrlen, salt, slen);
}

void unwrap_keyfile(struct keyfile* kf, char* phrase, int phrlen)
{
	hash_passphrase(kf, phrase, phrlen);
	memzero(phrase, sizeof(phrase));

	int slen = SALTLEN;
	void* wrapped = kf->buf + slen;
	int wraplen = kf->len - slen;

	aes128_unwrap(kf->kek, wrapped, wraplen);

	if(memcmp(wrapped, testpad, sizeof(testpad)))
		fail("incorrect passphrase", NULL, 0);
}

void copy_valid_iv(struct keyfile* kf)
{
	int ivoffset = SALTLEN;
	memcpy(kf->buf + ivoffset, testpad, sizeof(testpad));
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
	if(mem_off_cmp(sizeof(kf->buf), st.size) < 0)
		fail("keyfile too large", NULL, 0);

	if((ret = sys_read(fd, kf->buf, st.size)) < 0)
		fail("read", name, ret);
	if(ret < st.size)
		fail("incomplete read", NULL, 0);
	if(ret % 32 != 16)
		fail("invalid keyfile", name, 0);

	kf->len = st.size;
}

void write_keyfile(struct keyfile* kf, char* name, int flags)
{
	int wr, fd;
	int slen = SALTLEN;

	aes128_wrap(kf->kek, kf->buf + slen, kf->len - slen);

	if((fd = sys_open3(name, O_WRONLY | flags, 0600)) < 0)
		fail(NULL, name, fd);

	if((wr = writeall(fd, kf->buf, kf->len)) < 0)
		fail("write", name, wr);

	sys_close(fd);
	memzero(kf->buf, sizeof(kf->buf));
}

byte* get_key_by_idx(struct keyfile* kf, int idx)
{
	if(idx <= 0)
		fail("non-positive key index", NULL, 0);
	if(HDRSIZE + KEYSIZE*idx > kf->len)
		fail("key index out of range", NULL, 0);

	return kf->buf + HDRSIZE + KEYSIZE*(idx - 1);
}

int is_valid_key_idx(struct keyfile* kf, int idx)
{
	if(idx <= 0)
		return 0;
	if(HDRSIZE + KEYSIZE*idx > kf->len)
		return 0;
	return 1;
}
