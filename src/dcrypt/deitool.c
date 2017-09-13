#include <sys/file.h>

#include <crypto/aes128.h>

#include <errtag.h>
#include <format.h>
#include <util.h>

#include "keytool.h"

ERRTAG("deitool");

#define OPTS "de"
#define OPT_d (1<<0)
#define OPT_e (1<<1)

struct keyfile kf;

typedef void (*cryptf)(struct aes128* A, void* rp, void* wp, uint64_t S);

struct top {
	int ifd;
	int ofd;
	char* iname;
	char* oname;

	uint8_t* key;
	cryptf fn;
};

char ibuf[512];
char obuf[512];

#define CTX struct top* ctx

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

static void pipe_data(CTX)
{
	int rd, wr;
	int ifd = ctx->ifd;
	int ofd = ctx->ofd;

	cryptf fn = ctx->fn;
	uint64_t S = 0;

	struct aes128 A;

	aes128_init(&A, ctx->key);

	while((rd = sys_read(ifd, ibuf, 512)) > 0) {
		if(rd < 512)
			fail("incomplete read", NULL, 0);

		fn(&A, ibuf, obuf, S);

		if((wr = writeall(ofd, obuf, 512)) < 0)
			fail("write", ctx->oname, wr);

		S += 1;
	}

	aes128_fini(&A);
}

static int atoi(char* arg)
{
	char* p;
	int val;

	if(!(p = parseint(arg, &val)) || *p)
		fail("integer argument required:", arg, 0);
	
	return val;
}

static void load_keyfile(struct keyfile* kf, char* name)
{
	char phrase[80];
	int phrlen;

	read_keyfile(kf, name);
	phrlen = ask("Passphrase: ", phrase, sizeof(phrase));
	unwrap_keyfile(kf, phrase, phrlen);
}

static void setup(CTX, char* iname, char* oname, char* keyf, int kidx)
{
	int fd;

	if((fd = sys_open(iname, O_RDONLY)) < 0)
		fail(NULL, iname, fd);

	ctx->ifd = fd;
	ctx->iname = iname;

	load_keyfile(&kf, keyf);

	if(kidx < 1 || 16*kidx > kf.len)
		fail("bad keyindex", NULL, 0);

	if((fd = sys_open3(oname, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
		fail(NULL, oname, fd);

	ctx->ofd = fd;
	ctx->oname = oname;

	ctx->key = (uint8_t*)(kf.buf + 16*kidx);
}

int main(int argc, char** argv)
{
	int i = 1, opts = 0;
	struct top ctx;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(i + 3 > argc)
		fail("too few arguments", NULL, 0);
	if(i + 4 < argc)
		fail("too many arguments", NULL, 0);

	char* iname = argv[i++];
	char* oname = argv[i++];
	char* keyf = argv[i++];
	int kidx = i < argc ? atoi(argv[i++]) : 1;

	setup(&ctx, iname, oname, keyf, kidx);

	if(opts == OPT_d)
		ctx.fn = aesxts_decrypt;
	else if(opts == OPT_e)
		ctx.fn = aesxts_encrypt;
	else if(!opts)
		fail("no mode specified", NULL, 0);
	else
		fail("bad mode specified", NULL, 0);

	pipe_data(&ctx);

	return 0;
}
