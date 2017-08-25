#include <sys/file.h>

#include <errtag.h>
#include <output.h>
#include <util.h>

#define OPTS "nx"
#define OPT_n (1<<0)	/* minimal seq length */
#define OPT_x (1<<1)	/* do not print offsets */

ERRTAG("strings");

#define PAGE 4096

char inbuf[2*PAGE];

struct parsestate {
	int min;
	long seq;	/* current uninterrupted sequence length */
	long pos;	/* position within the file, for -x */
	long off;	/* offset of current sequence in file */
	char* buf;	/* string buffer */
};

static void xwriteout(char* buf, int len)
{
	xchk(writeout(buf, len), "write", NULL);
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

static void writeaddr(long off)
{
	char addr[10];

	fmtaddr32(addr, off);
	addr[8] = ' ';
	addr[9] = ' ';
	xwriteout(addr, 10);
}

/* This is basically innards of a state machine, pulled out of
   strings() where it belongs. Not pretty, but keeps nesting levels sane. */

void parseblock(struct parsestate* ps, char* data, int len, int showaddr)
{
	char* p = data;
	char* end = data + len;

	int min = ps->min;
	long seq = ps->seq;
	long off = ps->off;
	long pos = ps->pos;
	char* buf = ps->buf;

	for(; p < end; p++, pos++) {
		if(printable(*p)) {
			off = pos;
			if(seq == min && showaddr)
				writeaddr(off);
			if(seq < min)
				buf[seq] = *p;
			else if(seq == min)
				xwriteout(buf, seq);
			if(seq >= min)
				xwriteout(p, 1);
			seq++;
		} else {
			if(seq > min)
				xwriteout("\n", 1);
			seq = 0;
		}
	}

	ps->off = off;
	ps->pos = pos;
	ps->seq = seq;
}

static void strings(int minlen, long fd, int showaddr)
{
	long rd;
	char strbuf[minlen];
	struct parsestate ps = {
		.pos = 0,
		.seq = 0,
		.off = 0,
		.buf = strbuf,
		.min = minlen - 1
	};

	while((rd = sys_read(fd, inbuf, sizeof(inbuf))) > 0)
		parseblock(&ps, inbuf, rd, showaddr);
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

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;
	int minlen = 6;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(opts & OPT_n)
		minlen = xatou(argv[i++]);
	if(minlen <= 0 || minlen > 128)
		fail("bad min length value", NULL, 0);
	if(i < argc - 1)
		fail("too many arguments", NULL, 0);

	if(i < argc) {
		char* fn = argv[i];
		long fd = xchk(sys_open(fn, O_RDONLY), "cannot open", fn);
		strings(minlen, fd, !(opts & OPT_x));
	} else {
		strings(minlen, 0, !(opts & OPT_x));
	}
	flushout();

	return 0;
}
