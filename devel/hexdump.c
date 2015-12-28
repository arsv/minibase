#include <bits/errno.h>
#include <bits/fcntl.h>
#include <sys/open.h>
#include <sys/read.h>
#include <sys/write.h>

#include <fail.h>
#include <null.h>
#include <xchk.h>
#include <memcpy.h>
#include <fmtstr.h>
#include <fmtchar.h>

#define RDBUF (1<<12)
#define WRBUF (1<<14)
#define HEXLINE 16
#define OUTLINE 160

ERRTAG = "hexdump";
ERRLIST = { RESTASNUMBERS };

static const char hexdigits[] = "0123456789ABCDEF";

static void writeall(int fd, char* buf, long size)
{
	long wr;

	while(size > 0 && (wr = syswrite(fd, buf, size)) > 0) {
		buf += wr;
		size -= wr;
	} if(wr < 0) {
		fail("write failed", NULL, -wr);
	}
}

static char* fmtaddr(char* p, char* end, unsigned long addr)
{
	int i;

	for(i = 7; i >= 0; i--) {
		if(p >= end) 
			continue;
		int d = addr & 0x0F;
		*(p + i) = hexdigits[d];
		addr >>= 4;
	}

	p += 8;

	return p < end ? p : end;
}

static int isprintable(int c)
{
	unsigned char cc = (c & 0xFF);
	return (cc >= 0x20 && cc < 0x7F);
}

static char* fmthexch(char* p, char* end, char c)
{
	if(p < end) *p++ = hexdigits[((c >> 4) & 0x0F)];
	if(p < end) *p++ = hexdigits[((c >> 0) & 0x0F)];
	return p;
}

/* This adds a single dump line to the output buffer.
   The maximum length of the line is well known (~140 chars)
   and the buffer is expected to have at least that much
   space available. */

static char* makeline(char* p, char* end, unsigned long addr, char* data, int size)
{
	int i;

	p = fmtaddr(p, end, addr);
	p = fmtstr(p, end, "   ");

	for(i = 0; i < 16; i++) {
		if(i < size)
			p = fmthexch(p, end, *(data + i));
		else
			p = fmtstr(p, end, "  ");
		if(i == 7)
			p = fmtstr(p, end, "  ");
		else if(i != 15)
			p = fmtstr(p, end, " ");
	}

	p = fmtstr(p, end, "   ");
	
	for(i = 0; i < 16 && i < size; i++) {
		char c = *(data + i);
		p = fmtchar(p, end, isprintable(c) ? c : '.');
	}

	p = fmtchar(p, end, '\n');

	return p;
}

/* Several output lines (each 16 input bytes long) are merged
   into a single output block. There may be more than one output
   block per each dumpbuf() call. */

static void dumpbuf(unsigned long addr, char* data, long size)
{
	char buf[WRBUF];
	char* hwm = buf + sizeof(buf) - OUTLINE;
	char* end = buf + sizeof(buf);
	char* p = buf;

	char* dptr = data;
	char* dend = data + size;

	while(dptr < dend) {
		int linesize = (dptr + 16 < dend ? 16 : dend - dptr);
		p = makeline(p, end, addr, dptr, linesize);
		dptr += linesize;
		addr += linesize;

		if(p < hwm) continue;

		writeall(1, buf, p - buf);
		p = buf;
	} if(p > buf)
		writeall(1, buf, p - buf);
}

/* Input is read in large chunks, not necessary 16-byte aligned.
   The sequence of chunks is then re-arranged and dumpbuf() is called
   with strictly 16-aligned blocks. Except for the last block, which
   may be shorter. */

static void hexdump(long fd)
{
	char buf[RDBUF];
	unsigned long addr = 0;
	long ptr = 0;
	long rd;

	while((rd = sysread(fd, buf + ptr, sizeof(buf) - ptr)) > 0) {
		if((ptr += rd) < HEXLINE)
			continue;

		long rem = ptr % HEXLINE;
		long blk = ptr - rem;

		dumpbuf(addr, buf, blk);
		addr += blk;

		if(rem) {
			memcpy(buf, buf + blk, rem);
			ptr = rem;
		} else {
			ptr = 0;
		};
	} if(ptr) {
		dumpbuf(addr, buf, ptr);
	}
}

int main(int argc, char** argv)
{
	int i = 1;

	if(i >= argc) {
		hexdump(0);
	} else while(i < argc) {
		char* fn = argv[i++];
		long fd = xchk(sysopen(fn, O_RDONLY), "cannot open", fn);
		hexdump(fd);
		if(i < argc) syswrite(1, "\n", 1);
	}

	return 0;
}
