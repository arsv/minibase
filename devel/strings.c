#include <bits/errno.h>
#include <bits/fcntl.h>
#include <sys/open.h>
#include <sys/read.h>

#include <argbits.h>
#include <bufout.h>
#include <fail.h>
#include <null.h>
#include <xchk.h>

#define OPTS "nx"
#define OPT_n (1<<0)
#define OPT_x (1<<1)

ERRTAG = "strings";
ERRLIST = {
	REPORT(ENOENT), REPORT(EINVAL), REPORT(ENOSYS),
	RESTASNUMBERS
};

#define PAGE 4096

char inbuf[2*PAGE];
char outbuf[PAGE];

struct bufout bo;
struct parsestate {
	int min;
	long seq;	/* current uninterrupted sequence length */
	long pos;	/* position within the file, for -x */
	long off;	/* offset of current sequence in file */
	char* buf;	/* string buffer */
} ps;

static void xbufout(struct bufout* bo, char* buf, int len)
{
	xchk(bufout(bo, buf, len), "write", NULL);
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

/* This is basically innards of a state machine, pulled out of
   strings() where it belongs. Not pretty, but keeps nesting levels sane. */

void parseblock(char* buf, int len, int showaddr)
{
	char* p = buf;
	char* end = buf + len;

	for(; p < end; p++) {
		if(printable(*p)) {
			ps.off = ps.pos;
			if(ps.seq == ps.min && showaddr) {
				char addr[10];
				fmtaddr32(addr, ps.off);
				addr[8] = ' ';
				addr[9] = ' ';
				xbufout(&bo, addr, 10);
			}
			if(ps.seq < ps.min)
				ps.buf[ps.seq] = *p;
			else if(ps.seq == ps.min)
				xbufout(&bo, ps.buf, ps.seq);
			if(ps.seq >= ps.min)
				xbufout(&bo, p, 1);
			ps.seq++;
		} else {
			if(ps.seq > ps.min)
				xbufout(&bo, "\n", 1);
			ps.seq = 0;
		}
		ps.pos++;
	}
}

static void strings(int minlen, long fd, int showaddr)
{
	long rd;
	char strbuf[minlen];

	bo.fd = 1;
	bo.buf = outbuf;
	bo.len = sizeof(outbuf);
	bo.ptr = 0;

	ps.pos = 0;
	ps.seq = 0;
	ps.buf = strbuf;
	ps.min = minlen - 1;

	while((rd = sysread(fd, inbuf, sizeof(inbuf))) > 0)
		parseblock(inbuf, rd, showaddr);
	if(rd < 0)
		fail("read", NULL, -rd);

	bufoutflush(&bo);
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
		long fd = xchk(sysopen(fn, O_RDONLY), "cannot open", fn);
		strings(minlen, fd, (opts & OPT_x));
	} else {
		strings(minlen, 0, (opts & OPT_x));
	}

	return 0;
}
