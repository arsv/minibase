#include <sys/file.h>
#include <sys/mman.h>

#include <errtag.h>
#include <string.h>
#include <format.h>
#include <util.h>

#define BUFSIZE 16*4096
#define MAPSIZE 0x80000000

#define OPTS "lcw"
#define OPT_l (1<<0)	/* lines */
#define OPT_c (1<<1)	/* characters */
#define OPT_w (1<<2)	/* words */
/* non-option bits in opts */
#define SET_pad (1<<16)	/* to align output for several files */

ERRTAG("wc");
ERRLIST(NENOENT NEACCES NEFAULT NEFBIG NEINTR NEPIPE NEINVAL NEISDIR
	NELOOP NEMFILE NENFILE NENODEV NENOMEM NENOTDIR NEOVERFLOW NEBADF);

struct wc {
	uint64_t lines;
	uint64_t words;
	uint64_t bytes;
};

static void addcounts(struct wc* a, struct wc* c)
{
	a->lines += c->lines;
	a->words += c->words;
	a->bytes += c->bytes;
}

static const char* pad(uint64_t num, int to)
{
	static const char space[] = "\x20\x20\x20\x20\x20\x20";	
	static const int spacelen = sizeof(space) - 1;
	int width;

	for(width = 1; num >= 10 && width < to; width++)
		num /= 10;

	int padneeded = (width < to ? to - width : 0);

	if(padneeded > spacelen)
		padneeded = spacelen;

	return space + spacelen - padneeded;
}

static char* fmtpad1(char* p, char* e, uint64_t num, int to, int opts)
{
	if(opts & SET_pad)
		p = fmtstr(p, e, pad(num, to));
	return fmtu64(p, e, num);
}

/* Output looks like this:

   	42 169 997 filename

   The numbers are in the same order as in struct wc,
   and filename may be missing. */

static void dump(struct wc* cnts, const char* file, int opts)
{
	int flen = file ? strlen(file) : 0;

	char buf[3*16 + flen];
	char* end = buf + sizeof(buf) - 1;
	char* p = buf;

	if(opts & OPT_l) {
		p = fmtpad1(p, end, cnts->lines, 5, opts);
	} else if(opts & OPT_w) {
		p = fmtpad1(p, end, cnts->words, 5, opts);
	} else if(opts & OPT_c) {
		p = fmtpad1(p, end, cnts->bytes, 6, opts);
	} else {
		p = fmtpad1(p, end, cnts->lines, 5, opts);
		p = fmtstr(p, end, " ");
		p = fmtpad1(p, end, cnts->words, 5, opts);
		p = fmtstr(p, end, " ");
		p = fmtpad1(p, end, cnts->bytes, 6, opts);
	} if(file) {
		p = fmtstr(p, end, "  ");
		p = fmtstr(p, end, file);
	};

	*p++ = '\n';
	xchk(writeall(STDOUT, buf, p - buf), "write", NULL);
}

static int wordchar(char* p)
{
	unsigned char* q = (unsigned char*) p;

	return ((*q > 0x20) && (*q < 0x7F));
}

static void count(struct wc* cnts, char* buf, unsigned long len)
{
	char* p;
	char* end = buf + len;
	int inword = 0;
	struct wc c = { 0, 0, 0 };

	for(p = buf; p < end; p++) {
		/* XXX: this should be unicode count, total file size
		   is something stat should be used for, now wc. */
		c.bytes++;

		if(*p == '\n')
			c.lines++;
		if(wordchar(p)) {
			/* XXX: that's not what BB wc does */
			if(!inword) {
				inword = 1;
				c.words++;
			}
		} else {
			inword = 0;
		};
	}

	addcounts(cnts, &c);
}

static void countstdin(struct wc* cnts)
{
	const int len = BUFSIZE;

	const int prot = PROT_READ | PROT_WRITE;
	const int flags = MAP_PRIVATE;

	char* buf = sys_mmap(NULL, len, prot, flags, -1, 0);

	if(mmap_error(buf))
		fail("mmap", NULL, (long)buf);

	unsigned long rd;

	while((rd = sys_read(STDIN, buf, len)) > 0)
		count(cnts, buf, rd);
}

static void countfile(struct wc* cnts, const char* fname, int last)
{
	struct stat st;
	long fd = xchk(sys_open(fname, O_RDONLY), "open", fname);
	xchk(sys_fstat(fd, &st), "stat", fname);

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;

	char* buf = NULL; /* mmaped window */
	uint64_t off = 0; /* offset within the file */
	long map = 0; /* bytes mmapped on this iter */
	uint64_t rem = st.size; /* bytes remaining */

	while(off < st.size) {
		map = rem > MAPSIZE ? MAPSIZE : rem;

		buf = sys_mmap(buf, map, prot, flags, fd, off);

		if(mmap_error(buf))
			fail("mmap", fname, (long)buf);

		off += map;
		rem -= map;

		count(cnts, buf, map);
	};

	if(last) return;

	xchk(sys_munmap(buf, off > MAPSIZE ? MAPSIZE : map), "munmap", fname);
	xchk(sys_close(fd), "close", fname);
}

static int countbits(long val, int bits)
{
	int count = 0;
	long mask;

	for(mask = 1; bits-- > 0; mask = mask << 1)
		if(val & mask)
			count++;

	return count;
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;
	struct wc cnts = { 0, 0, 0 };

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(countbits(opts & (OPT_l | OPT_w | OPT_c), 5) > 1)
		fail("cannot use more than one of -lwc at the same time", NULL, 0);

	if(i >= argc) {
		/* no arguments, count stdin */
		countstdin(&cnts);
		dump(&cnts, NULL, opts);
	} else if(i == argc - 1) {
		/* single file, count it but do not do totals */
		countfile(&cnts, argv[i], 1);
		dump(&cnts, argv[i], opts);
	} else {
		/* more than one file, got to print totals */
		struct wc total = { 0, 0, 0 };
		/* and try to make sure the numbers line up */
		opts |= SET_pad;

		for(; i < argc; i++) {
			int lastone = (i == argc - 1);
			countfile(&cnts, argv[i], lastone);
			dump(&cnts, argv[i], opts);
			addcounts(&total, &cnts);
		};
		
		dump(&total, NULL, opts);
	}

	return 0;
}
