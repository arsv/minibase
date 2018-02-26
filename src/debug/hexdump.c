#include <sys/file.h>

#include <string.h>
#include <format.h>
#include <errtag.h>
#include <util.h>

#define RDBUF (1<<12)
#define WRBUF (1<<14)
#define HEXLINE 16
#define OUTLINE 160

ERRTAG("hexdump");
ERRLIST(NENOENT NEACCES NENOTDIR NELOOP NEISDIR NEFAULT NEINVAL NENOMEM
        NEBADF NEOVERFLOW);

/* The values between 0x20 and 0x7E should be safe to print on pretty
   much any terminal. Everything else is shown as ".", and we do not
   bother with UTF-8 sequences. Hexdump is not the kind of tool that
   should be handling UTF-8 anyway. */

static int isprintable(int c)
{
	unsigned char cc = (c & 0xFF);
	return (cc >= 0x20 && cc < 0x7F);
}

/* The address (offset within the file) is always shown as a 4-byte value.
   Hexdumping something over 4GB is a bad idea.
   Using less than 4 bytes would make sense, but traditionally it has been
   4 bytes so we keep that. Also, the first alternative would be 2 bytes
   which is too few for actual real life usage. */

static char* fmtaddr(char* p, char* e, unsigned long addr)
{
	int i;

	for(i = 3; i >= 0; i--)
		p = fmtbyte(p, e, (addr >> 8*i) & 0xFF);

	return p;
}

/* A single output line looks like this:

   00000000   01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ................

   The maximum length of the line is well known (~72)
   and the buffer is expected to have at least that much
   space available. */

static char* makeline(char* p, char* e, unsigned long addr, char* data, int size)
{
	int i;

	p = fmtaddr(p, e, addr);
	p = fmtstr(p, e, "   ");

	for(i = 0; i < 16; i++) {
		if(i < size)
			p = fmtbyte(p, e, *(data + i));
		else
			p = fmtstr(p, e, "  ");
		if(i == 7)
			p = fmtstr(p, e, "  ");
		else if(i != 15)
			p = fmtstr(p, e, " ");
	}

	p = fmtstr(p, e, "   ");

	for(i = 0; i < 16 && i < size; i++) {
		char c = *(data + i);
		p = fmtchar(p, e, isprintable(c) ? c : '.');
	}

	p = fmtchar(p, e, '\n');

	return p;
}

/* Several output lines (each 16 input bytes long) are merged
   into a single output block, to avoid excessive sys_write()s.
   There may be more than one output block per each dumpbuf()
   call however */

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
   may be shorter.

   The way it is written it may do one sys_write() more than necessary,
   but it should do a reasonably good job at handling slowly-piped data.

   Not sure if that's important, but then again, hexdump is not something
   that should be well-optimized anyway. */

static void hexdump(long fd)
{
	char buf[RDBUF];
	unsigned long addr = 0;
	long ptr = 0;
	long rd;

	while((rd = sys_read(fd, buf + ptr, sizeof(buf) - ptr)) > 0) {
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

static void dumpfile(const char* name)
{
	int fd;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	hexdump(fd);
}

/* Handling more than a single file probably makes no sense? */

int main(int argc, char** argv)
{
	if(argc == 1)
		hexdump(0);
	else if(argc == 2)
		dumpfile(argv[1]);
	else
		fail("too many arguments", NULL, 0);

	return 0;
}
