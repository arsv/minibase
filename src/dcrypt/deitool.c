#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/proc.h>
#include <sys/prctl.h>
#include <sys/sched.h>

#include <crypto/aes128.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "keytool.h"

ERRTAG("deitool");

#define OPTS "ds"
#define OPT_d (1<<0)
#define OPT_s (1<<1)

struct keyfile kf;

struct top {
	int argc;
	int argi;
	char** argv;
	int opts;

	int ifd;
	int ofd;
	char* iname;
	char* oname;
	struct aes128 K1; /* data key */
	struct aes128 K2; /* tweak key */

	int ncpus;
	uint64_t size;
};

char ibuf[512];
char obuf[512];

#define CTX struct top* ctx

typedef void (*cryptf)(CTX, void* rp, void* wp, uint64_t S);

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

static void aesxts(CTX, void* rp, void* wp, uint64_t S, aesfn op)
{
	uint8_t T[16];
	uint8_t x[16];

	prep_tweak(T, &ctx->K2, S);

	for(int i = 0; i < 512; i += 16) {
		blk_xor(x, rp + i, T);
		op(&ctx->K1, x);
		blk_xor(wp + i, x, T);

		next_tweak(T);
	}
}

static void aesxts_encrypt(CTX, void* rp, void* wp, uint64_t S)
{
	aesxts(ctx, rp, wp, S, aes128_encrypt);
}

static void aesxts_decrypt(CTX, void* rp, void* wp, uint64_t S)
{
	aesxts(ctx, rp, wp, S, aes128_decrypt);
}

static void pipe_data_single(CTX, cryptf fn)
{
	int rd, wr;
	int ifd = ctx->ifd;
	int ofd = ctx->ofd;
	uint64_t S = 0;

	while((rd = sys_read(ifd, ibuf, 512)) > 0) {
		if(rd < 512)
			fail("incomplete read", NULL, 0);

		fn(ctx, ibuf, obuf, S);

		if((wr = writeall(ofd, obuf, 512)) < 0)
			fail("write", ctx->oname, wr);

		S += 1;
	}
}

static void pipe_data_child(CTX, cryptf fn, int n, int i)
{
	int ifd = ctx->ifd;
	int ofd = ctx->ofd;

	uint64_t off = i*512;
	uint64_t end = ctx->size;
	uint64_t S = i;

	int rd, wr;

	while(off < end) {
		if((rd = sys_pread(ifd, ibuf, 512, off)) < 0)
			fail("read", ctx->iname, rd);
		if(rd < 512)
			fail("incomplete read", NULL, 0);

		fn(ctx, ibuf, obuf, S);

		if((wr = sys_pwrite(ofd, obuf, 512, off)) < 0)
			fail("write", ctx->oname, wr);

		S += n;
		off += n*512;
	}
}

static void pipe_data_multi(CTX, cryptf fn)
{
	int i, nc = 0, n = ctx->ncpus;
	int pid, status;
	int ret, failed = 0;

	for(i = 0; i < n; i++) {
		if((pid = sys_fork()) < 0) {
			warn("fork", NULL, pid);
			failed = 1;
		} else if(pid == 0) {
			sys_prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
			pipe_data_child(ctx, fn, n, i);
			_exit(0);
		} else {
			nc++;
		}
	}

	for(i = 0; i < nc; i++) {
		if((ret = sys_waitpid(-1, &status, 0)) < 0)
			fail("wait", NULL, ret);
		if(status)
			failed = 1;
	}

	if(failed) _exit(0xFF);
}

static void pipe_data(CTX, cryptf fn)
{
	if(ctx->ncpus > 1)
		pipe_data_multi(ctx, fn);
	else
		pipe_data_single(ctx, fn);
}

static void load_keyfile(struct keyfile* kf, char* name)
{
	char phrase[80];
	int phrlen;

	read_keyfile(kf, name);
	phrlen = ask("Passphrase: ", phrase, sizeof(phrase));
	unwrap_keyfile(kf, phrase, phrlen);
}

static int guess_num_cpus(void)
{
	struct cpuset cs;
	int ret;

	memzero(&cs, sizeof(cs));

	if((ret = sys_sched_getaffinity(0, &cs)) < 0)
		return 1;

	uint w, b, cpus = 0;

	for(w = 0; w < ARRAY_SIZE(cs.bits); w++)
		for(b = 0; b < 8*sizeof(cs.bits[0]); b++)
			if(cs.bits[w] & (1UL << b))
				cpus++;

	return cpus;
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

	if(opts & OPT_s)
		ctx->ncpus = 1;
	else
		ctx->ncpus = guess_num_cpus();
}

static void set_files(CTX, char* iname, char* oname, char* keyf, int kidx)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(iname, O_RDONLY)) < 0)
		fail(NULL, iname, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", iname, ret);
	if(st.size % 512)
		fail(iname, "size is not in 512-blocks", 0);

	ctx->ifd = fd;
	ctx->iname = iname;
	ctx->size = st.size;

	load_keyfile(&kf, keyf);

	if((fd = sys_open3(oname, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
		fail(NULL, oname, fd);

	ctx->ofd = fd;
	ctx->oname = oname;

	if(ctx->ncpus <= 1)
		;
	else if((ret = sys_ftruncate(fd, st.size)) < 0)
		fail("truncate", oname, ret);

	byte* key = get_key_by_idx(&kf, kidx);

	aes128_init(&ctx->K1, key + 0);
	aes128_init(&ctx->K2, key + 16);
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

	return 0;
}
