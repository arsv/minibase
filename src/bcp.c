#include <sys/open.h>
#include <sys/fstat.h>
#include <sys/mmap.h>
#include <sys/munmap.h>
#include <sys/write.h>
#include <sys/ioctl.h>
#include <sys/ftruncate.h>
#include <sys/fallocate.h>
#include <sys/sendfile.h>
#include <sys/lseek.h>
#include <sys/close.h>
#include <sys/read.h>
#include <bits/types.h>

#include <argbits.h>
#include <parselong.h>
#include <memcpy.h>
#include <writeall.h>
#include <fail.h>

ERRTAG = "bcp";
ERRLIST = { 
	REPORT(EAGAIN), REPORT(EBADF), REPORT(EFAULT), REPORT(EINTR),
	REPORT(EINVAL), REPORT(EIO), REPORT(EISDIR), REPORT(EDQUOT),
	REPORT(EFBIG), REPORT(ENOSPC), REPORT(EPERM), REPORT(EPIPE),
	REPORT(ENOENT), REPORT(EEXIST),
	RESTASNUMBERS
};

#define BLKGETSIZE64 _IOR(0x12,114,size_t)

#define OPTS "rwz"
#define OPT_r (1<<0)
#define OPT_w (1<<1)
#define OPT_z (1<<2)

#define SET_size (1<<16)

#define MAXRUN 10*1024*1024

struct file {
	char* name;
	int fd;
	int type;
	uint64_t size;
	uint64_t off;
};

struct bcp {
	struct file l;	/* left argument */
	struct file r;	/* right argument */
	uint64_t size;
	uint64_t unit;
	int opts;
};

/* 1k@2:2 x1M */

static const char* parsesuffixed(uint64_t* u, const char* n, long unit)
{
	uint64_t tmp = 0;
	const char* p;
	int d;

	if(!n) fail("numeric argument required", NULL, 0);

	for(p = n; *p; p++)
		if(*p >= '0' && (d = *p - '0') <= 9)
			tmp = tmp*10 + d;
		else
			break;

	switch(*p) {
		case 'G': tmp *= 1024*1024*1024; p++; break;
		case 'M': tmp *= 1024*1024; p++; break;
		case 'k':
		case 'K': tmp *= 1024; p++; break;
		case 'b':
		case 'B': p++; break;
		default: tmp *= unit;
	}

	*u = tmp;

	return p;
}

static void parseunit(struct bcp* ctx, const char* arg)
{
	const char* p = arg;

	if(*p == 'x')
		p = parsesuffixed(&ctx->unit, p+1, 1);
	if(*p)
		fail("bad unit spec:", arg, 0);
}

static void parsespec(struct bcp* ctx, const char* arg)
{
	const char* p = arg;
	uint64_t unit = ctx->unit;

	if(*p >= '0' && *p <= '9') {
		p = parsesuffixed(&ctx->size, p, unit);
		ctx->opts |= SET_size;
	} if(*p == '@') {
		p = parsesuffixed(&ctx->l.off, p+1, unit);
	} if(*p == ':') {
		p = parsesuffixed(&ctx->r.off, p+1, unit);
	} if(*p) {
		fail("bad range spec", arg, 0);
	}
}

static void parseopts(struct bcp* ctx, int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(i < argc)
		ctx->l.name = argv[i++];
	else
		fail("too few arguments", NULL, 0);

	if(opts & OPT_z)
		ctx->r.name = NULL;
	else if(i < argc)
		ctx->r.name = argv[i++];
	else
		fail("too few arguments", NULL, 0);

	ctx->opts = opts;
	ctx->size = 0;
	ctx->l.off = 0;
	ctx->r.off = 0;
	ctx->unit = 1;

	int j = argc - 1;

	if(j > i && argv[j][0] == 'x')
		parseunit(ctx, argv[j--]);
	if(j >= i)
		parsespec(ctx, argv[j--]);
	if(j >= i)
		fail("too many arguments", NULL, 0);
}

static char* mmapempty(long size)
{
	long ret = sysmmap(NULL, size, PROT_READ, MAP_ANONYMOUS, -1, 0);	

	if(MMAPERROR(ret))
		fail("mmap", NULL, ret);

	return (char*)ret;
}

/* Struct file utils */

static void openstat(struct file* f, long flags)
{
	struct stat st;

	long fd = xchk(sysopen3(f->name, flags, 0666), "cannot open", f->name);

	xchk(sysfstat(fd, &st), "cannot stat", f->name);

	f->fd = fd;
	f->type = st.st_mode & S_IFMT;

	if(f->type == S_IFBLK) {
		xchk(sysioctl(fd, BLKGETSIZE64, (long)&(f->size)),
			"cannot get size of", f->name);
	} else {
		f->size = st.st_size;
	}
}

static void truncate(struct file* dst, uint64_t size)
{
	xchk(sysftruncate(dst->fd, size), "cannot truncate", dst->name);
}

static void seekfile(struct file* f)
{
	if(!f->off)
		return;
	xchk(syslseek(f->fd, f->off, 0),
		"cannot seek", f->name);
}

static void closefile(struct file* f)
{
	xchk(sysclose(f->fd), "cannot close", f->name);
}

static int sizable(struct file* f)
{
	int type = f->type;
	return (type == S_IFREG || type == S_IFBLK);
}

static int regular(struct file* f)
{
	return f->type == S_IFREG;
}

/* Data copy mode (-r and -w) */

static int checksize(struct file* f, uint64_t size)
{
	if(!sizable(f)) return 1;
	uint64_t end = f->off + size;
	return (end <= f->size);
}

static int sendfile(struct file* dst, struct file* src, uint64_t size)
{
	uint64_t rd = 0;
	uint64_t left = size;

	if(!sizable(src))
		return 0;

	while(left > 0) {
		uint64_t part = left > MAXRUN ? MAXRUN : left;

		if((rd = syssendfile(dst->fd, src->fd, NULL, part)) < 0)
			break;

		left -= rd;
	}

	if(rd == -EINVAL && left == size)
		return 0;
	else if(rd < 0)
		fail("sendfile", NULL, rd);
	else
		return 1;
}

static int copymmap(struct file* dst, struct file* src, uint64_t size)
{
	uint64_t rd = 0;
	uint64_t left = size;
	uint64_t soff = src->off;
	int sfd = src->fd;

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;

	if(!sizable(src))
		return 0;

	while(left > 0) {
		long part = left > MAXRUN ? MAXRUN : left;

		long addr = sysmmap(NULL, part, prot, flags, sfd, soff);
		void* buf = (void*)addr;

		if(MMAPERROR(addr))
			fail("mmap", src->name, addr);

		if((rd = writeall(dst->fd, buf, part)) < 0)
			fail("write", dst->name, rd);

		xchk(sysmunmap(buf, part), "munmap", src->name);

		left -= rd;
		soff += rd;
	}

	return 1;
}

static void readwrite(struct file* dst, struct file* src, uint64_t size)
{
	uint64_t left = size;

	long blen = size > MAXRUN ? MAXRUN : size;
	char* buf = mmapempty(blen);

	long rd, wr;
	uint64_t part;

	while(left > 0) {
		part = left > blen ? blen : left;

		if((rd = sysread(src->fd, buf, part)) <= 0)
			fail("read", src->name, rd);

		if((wr = writeall(dst->fd, buf, rd)) <= 0)
			fail("write", dst->name, wr);

		left -= wr;
	}

	sysmunmap(buf, blen);
}

static void transfer(struct bcp* ctx, struct file* dst, struct file* src)
{
	seekfile(dst);
	seekfile(src);

	uint64_t size = ctx->size;

	if(sendfile(dst, src, size))
		;
	else if(copymmap(dst, src, size))
		;
	else readwrite(dst, src, size);

	closefile(dst);
	closefile(src);
}

static void wmode(struct bcp* ctx)
{
	struct file* dst = &ctx->l;
	struct file* src = &ctx->r;
	int opts = ctx->opts;
	int setsize = opts & SET_size;

	openstat(src, O_RDONLY);

	if(!setsize && sizable(src))
		ctx->size = src->size;
	else if(!setsize)
		fail("transfer size must be specified for", src->name, 0);

	openstat(dst, O_WRONLY);

	if(!sizable(dst))
		fail("refusing to write to", dst->name, 0);
	if(!checksize(dst, ctx->size))
		fail("refusing to write past EOF in", dst->name, 0);
	if(!checksize(src, src->size))
		fail("cannot read past EOF in", src->name, 0);

	transfer(ctx, dst, src);
}

static void rmode(struct bcp* ctx)
{
	struct file* src = &ctx->l;
	struct file* dst = &ctx->r;
	int opts = ctx->opts;
	int setsize = opts & SET_size;

	openstat(src, O_RDONLY);

	if(!setsize && sizable(src))
		ctx->size = src->size;
	else if(!setsize)
		fail("transfer size must be specified for", src->name, 0);
	if(!checksize(src, ctx->size))
		fail("cannot read past EOF in", src->name, 0);

	openstat(dst, O_WRONLY | O_CREAT);

	if(regular(dst) && ctx->size + dst->off >= dst->size)
		truncate(dst, dst->off);

	transfer(ctx, dst, src);
}

/* Zero mode (-z) routines */

static void truncate2(struct file* dst, uint64_t size)
{
	truncate(dst, dst->off);
	truncate(dst, dst->off + size);
}

static int zerohole(struct file* dst, uint64_t size)
{
	const int flags = FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE;

	long ret = sysfallocate(dst->fd, flags, dst->off, size);

	if(!ret)
		return 1;
	if(ret == -EOPNOTSUPP || ret == -ENOSYS)
		return 0;

	fail("cannot fallocate", dst->name, ret);
}

static void zerommap(struct file* dst, uint64_t size)
{
	char* zeroes = mmapempty(size > MAXRUN ? MAXRUN : size);

	seekfile(dst);

	while(size > 0) {
		long run = size > MAXRUN ? MAXRUN : size;

		long wrt = syswrite(dst->fd, zeroes, run);
			
		if(wrt <= 0) /* 0 is not ok here */
			fail("cannot write to", dst->name, wrt);

		size -= wrt;
	}
}

/* Plain -z mode: create a new zero-filled file of a given size */

static void zmode(struct bcp* ctx)
{
	struct file* dst = &ctx->l;

	if(!(ctx->opts & SET_size))
		fail("size must be specified with -z", NULL, 0);

	dst->fd = xchk(sysopen3(dst->name, O_WRONLY | O_CREAT | O_EXCL, 0666),
			"cannot create", dst->name);

	truncate(dst, ctx->size);
	closefile(dst);
}

/* Zero-write a chunk in a block dev or a block dev image */

static void zwmode(struct bcp* ctx)
{
	struct file* dst = &ctx->l;
	int opts = ctx->opts;
	int setsize = opts & SET_size;

	openstat(dst, O_WRONLY);

	uint64_t size = setsize ? ctx->size : dst->size - dst->off;
	uint64_t end = dst->off + size;

	if(end > dst->size)
		fail("refusing to write past EOF in", dst->name, 0);

	if(regular(dst)) {
		if(end == dst->size)
			truncate2(dst, size);
		else if(zerohole(dst, size))
			;
		else zerommap(dst, size);
	} else {
		zerommap(dst, size);
	}

	closefile(dst);
}

/* Zero-resize/rewrite a file, only for regular files */

static void zrmode(struct bcp* ctx)
{
	struct file* dst = &ctx->l;
	int opts = ctx->opts;
	int setsize = opts & SET_size;

	openstat(dst, O_WRONLY);
	
	uint64_t size = setsize ? ctx->size : dst->size - dst->off;

	if(!regular(dst))
		fail("not a regular file:", dst->name, 0);

	truncate2(dst, size);
}

int main(int argc, char** argv)
{
	struct bcp ctx;

	parseopts(&ctx, argc, argv);

	int mode = ctx.opts & (OPT_r | OPT_w | OPT_z);

	switch(mode) {
		case OPT_z: zmode(&ctx); break;
		case OPT_z | OPT_w: zwmode(&ctx); break;
		case OPT_z | OPT_r: zrmode(&ctx); break;
		case OPT_r: rmode(&ctx); break;
		case OPT_w: wmode(&ctx); break;
		default:
			fail("one of -rwz must be used", NULL, 0);
	}

	return 0;
}
