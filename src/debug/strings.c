#include <sys/file.h>

#include <output.h>
#include <string.h>
#include <util.h>
#include <main.h>

#define OPTS "nx"
#define OPT_n (1<<0)	/* minimal seq length */
#define OPT_x (1<<1)	/* do not print offsets */

ERRTAG("strings");

#define PAGE 4096

char inbuf[2*PAGE];
char outbuf[PAGE];

struct top {
	int addr;
	int opts;

	int min;
	long seq;	/* current uninterrupted sequence length */
	long pos;	/* position within the file, for -x */
	long off;	/* offset of current sequence in file */
	char* buf;	/* string buffer */

	struct bufout bo;
};

#define CTX struct top* ctx

static void output(CTX, char* buf, int len)
{
	bufout(&ctx->bo, buf, len);
}

static int printable(char c)
{
	return ((c >= 0x20 && c < 0x7F) || c == '\t');
}

/* Same as with hexdump, the address is always shown as a 4-byte value. */

static const char hexdigits[] = "0123456789ABCDEF";

static void fmtaddr32(char* p, unsigned long addr)
{
	int i;
	for(i = 7; i >= 0; i--) {
		int d = addr & 0x0F;
		*(p + i) = hexdigits[d];
		addr >>= 4;
	}
}

static void write_addr(CTX, long off)
{
	char addr[10];

	fmtaddr32(addr, off);
	addr[8] = ' ';
	addr[9] = ' ';

	output(ctx, addr, 10);
}

/* This is basically innards of a state machine, pulled out of
   strings() where it belongs. Not pretty, but keeps nesting levels sane. */

void scan_block(CTX, char* data, int len)
{
	char* p = data;
	char* end = data + len;

	int min = ctx->min;
	long seq = ctx->seq;
	long off = ctx->off;
	long pos = ctx->pos;
	char* buf = ctx->buf;

	for(; p < end; p++, pos++) {
		if(printable(*p)) {
			off = pos;
			if(seq == min && ctx->addr)
				write_addr(ctx, off);
			if(seq < min)
				buf[seq] = *p;
			else if(seq == min)
				output(ctx, buf, seq);
			if(seq >= min)
				output(ctx, p, 1);
			seq++;
		} else {
			if(seq > min)
				output(ctx, "\n", 1);
			seq = 0;
		}
	}

	ctx->off = off;
	ctx->pos = pos;
	ctx->seq = seq;
}

static void scan_strings(CTX, int fd, int minlen, int opts)
{
	char strbuf[minlen];
	int rd;

	ctx->buf = strbuf;
	ctx->min = minlen - 1;
	ctx->opts = opts;

	ctx->addr = !(opts & OPT_x);

	while((rd = sys_read(fd, inbuf, sizeof(inbuf))) > 0)
		scan_block(ctx, inbuf, rd);
	if(rd < 0)
		fail("read", NULL, rd);
}

static unsigned int xatou(const char* p)
{
	const char* orig = p;
	int n = 0, d;

	if(!*p)
		fail("number expected", NULL, 0);
	else for(; *p; p++)
		if(*p >= '0' && (d = *p - '0') < 10)
			n = n*10 + d;
		else
			fail("not a number: ", orig, 0);

	return n;
}

static int open_check(const char* name)
{
	int fd;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	return fd;
}

static void init_output(CTX)
{
	struct bufout* bo = &ctx->bo;

	bo->fd = STDOUT;
	bo->buf = outbuf;
	bo->ptr = 0;
	bo->len = sizeof(outbuf);
}

static void fini_output(CTX)
{
	bufoutflush(&ctx->bo);
}

int main(int argc, char** argv)
{
	int i = 1, opts = 0;
	int minlen = 6;
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(opts & OPT_n)
		minlen = xatou(argv[i++]);
	if(minlen <= 0 || minlen > 128)
		fail("bad min length value", NULL, 0);
	if(i >= argc)
		fail("too few arguments", NULL, 0);
	if(i < argc - 1)
		fail("too many arguments", NULL, 0);

	int fd = open_check(argv[i]);

	init_output(ctx);
	scan_strings(ctx, fd, minlen, opts);
	fini_output(ctx);

	return 0;
}
