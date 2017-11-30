#include <sys/file.h>

#include <crypto/aes128.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "keytool.h"

ERRTAG("deitool");

#define OPTS "d"
#define OPT_d (1<<0)

struct keyfile kf;

typedef void (*cryptf)(struct aes128* A, void* rp, void* wp, uint64_t S);

struct top {
	int argc;
	int argi;
	char** argv;
	int opts;

	int ifd;
	int ofd;
	char* iname;
	char* oname;
	struct aes128 aes;
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

static void pipe_data(CTX, cryptf fn)
{
	int rd, wr;
	int ifd = ctx->ifd;
	int ofd = ctx->ofd;
	uint64_t S = 0;
	struct aes128* A = &ctx->aes;

	while((rd = sys_read(ifd, ibuf, 512)) > 0) {
		if(rd < 512)
			fail("incomplete read", NULL, 0);

		fn(A, ibuf, obuf, S);

		if((wr = writeall(ofd, obuf, 512)) < 0)
			fail("write", ctx->oname, wr);

		S += 1;
	}
}

static void load_keyfile(struct keyfile* kf, char* name)
{
	char phrase[80];
	int phrlen;

	read_keyfile(kf, name);
	phrlen = ask("Passphrase: ", phrase, sizeof(phrase));
	unwrap_keyfile(kf, phrase, phrlen);
}

static void init_context(CTX, int argc, char** argv)
{
	int i = 1, opts = 0;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->argi = i;
	ctx->argc = argc;
	ctx->argv = argv;
	ctx->opts = opts;
}

static void set_files(CTX, char* iname, char* oname, char* keyf, int kidx)
{
	int fd;

	if((fd = sys_open(iname, O_RDONLY)) < 0)
		fail(NULL, iname, fd);

	ctx->ifd = fd;
	ctx->iname = iname;

	load_keyfile(&kf, keyf);

	if((fd = sys_open3(oname, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
		fail(NULL, oname, fd);

	ctx->ofd = fd;
	ctx->oname = oname;

	byte* key = get_key_by_idx(&kf, kidx);

	aes128_init(&ctx->aes, key);
}

static void fini_context(CTX)
{
	aes128_fini(&ctx->aes);
}

static char* shift_arg(CTX)
{
	if(ctx->argi >= ctx->argc)
		return NULL;
	return ctx->argv[ctx->argi++];
}

static char* shift_req(CTX)
{
	if(ctx->argi >= ctx->argc)
		fail("too few arguments", NULL, 0);
	return ctx->argv[ctx->argi++];
}

static int maybe_shift_int(CTX, int dflt)
{
	char *arg, *p;
	int val;

	if(!(arg = shift_arg(ctx)))
		return dflt;

	if(!(p = parseint(arg, &val)) || *p)
		fail("integer required:", arg, 0);

	return val;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	init_context(ctx, argc, argv);

	char* iname = shift_req(ctx);
	char* oname = shift_req(ctx);
	char* keyf = shift_req(ctx);
	int kidx = maybe_shift_int(ctx, 1);

	set_files(ctx, iname, oname, keyf, kidx);

	if(ctx->opts & OPT_d)
		pipe_data(ctx, aesxts_decrypt);
	else
		pipe_data(ctx, aesxts_encrypt);

	fini_context(ctx);

	return 0;
}
