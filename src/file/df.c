#include <sys/statfs.h>
#include <sys/open.h>
#include <sys/read.h>

#include <argbits.h>
#include <writeout.h>
#include <memcpy.h>
#include <strlen.h>
#include <strcbrk.h>
#include <fmtlong.h>
#include <fmtstr.h>
#include <fmtchar.h>
#include <fail.h>

ERRTAG = "df";
ERRLIST = { RESTASNUMBERS };

char minbuf[4096]; /* mountinfo */

long xopen(const char* fname, int flags)
{
	return xchk(sysopen(fname, flags), "cannot open", fname);
}

void xwriteout(char* buf, int len)
{
	xchk(writeout(buf, len), "write", NULL);
}

static char* fmt4is(char* p, char* e, int n)
{
	if(p + 3 >= e) return e;

	*(p+3) = '0' + (n % 10); n /= 10;
	*(p+2) = n ? ('0' + n % 10) : ' '; n /= 10;
	*(p+1) = n ? ('0' + n % 10) : ' '; n /= 10;
	*(p+0) = n ? ('0' + n % 10) : ' ';

	return p + 4;
}

static char* fmt1i0(char* p, char* e, int n)
{
	if(p < e) *p++ = '0' + (n % 10);
	return p;
}

static char* fmtmem(char* p, char* e, unsigned long n, int mu)
{
	static const char sfx[] = "BKMGTP";

	/* mu is typically 4KB or so, and we can skip three extra zeros */
	int muinkb = !(mu % 1024);
	int sfi = muinkb ? 1 : 0;
	unsigned long nb = muinkb ? n*(mu/1024) : n*mu;
	unsigned long fr = 0;

	/* find out the largest multiplier we can use */
	for(; sfi < sizeof(sfx) && nb > 1024; sfi++) {
		fr = nb % 1024;
		nb /= 1024;
	}
	
	if(sfi >= sizeof(sfx)) {
		/* it's too large; format the number and be done with it */
		p = fmtlong(p, e, nb);
		p = fmtchar(p, e, sfx[sizeof(sfx)-1]);
	} else {
		/* it's manageable; do nnnn.d conversion */
		fr = fr*10/1024; /* one decimals */
		p = fmt4is(p, e, nb);
		p = fmtchar(p, e, '.');
		p = fmt1i0(p, e, fr);
		p = fmtchar(p, e, sfx[sfi]);
	}

	return p;
}

/* The output should look like something like this:

   Size   Used    Free         Mountpoint
   9.8G   4.5G    5.3G   47%   /

   The numbers should be aligned on decimal point. */

static void wrheader()
{
	static const char hdr[] = 
	    /* |1234.1x_| */
	       "   Size "
	       "   Used "
	       "   Free"
	    /* |   12%   | */
	       "   Use   "
	       "Mountpoint\n";

	xwriteout((char*)hdr, sizeof(hdr));
}

static char* fmtstatfs(char* p, char* e, struct statfs* st, int opts)
{
	long bs = st->f_bsize;
	long blocksused = st->f_blocks - st->f_bavail;
	long blockstotal = st->f_blocks;
	long perc = 100*blocksused/blockstotal;

	p = fmtmem(p, e, st->f_blocks, bs);
	p = fmtstr(p, e, " ");
	p = fmtmem(p, e, st->f_blocks - st->f_bavail, bs);
	p = fmtstr(p, e, " ");
	p = fmtmem(p, e, st->f_bavail, bs);
	p = fmtstr(p, e, "   ");
	p = fmtlong(p, e, perc);
	p = fmtstr(p, e, "%   ");

	return p;
}

static void reportfs(char* mountpoint, char* devid, int opts)
{
	struct statfs st;

	/* Major 0 means vfs. Used/free space counts are meaningless
	   for most of them. */
	if(devid[0] == '0' && devid[1] == ':')
		return;

	xchk(sysstatfs(mountpoint, &st), "statfs", mountpoint);

	int len = strlen(mountpoint) + 100;
	char buf[len];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstatfs(p, e, &st, opts);
	p = fmtstr(p, e, mountpoint);
	*p++ = '\n';
	
	xwriteout(buf, p - buf);
}

/* A line from mountinfo looks like this:

   66 21 8:3 / /home rw,noatime,nodiratime shared:25 - ext4 /dev/sda3 rw,stripe=128,data=ordered

   We need fields [2] devid, [4] mountpoint, and possibly [8] device.
   The fields are presumed to be space-separated, with no spaces within.
   If they aren't, we'll get garbage.
 
   Actually, we don't need [8] device. Too far to the right. */

static void scanline(char* line, int opts)
{
	char* parts[5];
	int i;

	char* p = line;
	char* q;

	for(i = 0; i < 5; i++) {
		if(!*(q = strcbrk(p, ' ')))
			break;
		parts[i] = p;
		*q = '\0'; p = q + 1;
	} if(i < 5) {
		return;
	}

	reportfs(parts[4], parts[2], opts);
}

static char* findline(char* p, char* e)
{
	while(p < e)
		if(*p == '\n')
			return p;
		else
			p++;
	return NULL;
}

static void scanall(int opts)
{
	long fd = xopen("/proc/self/mountinfo", O_RDONLY);
	long rd;
	long of = 0;

	while((rd = sysread(fd, minbuf + of, sizeof(minbuf) - of)) > 0) {
		char* p = minbuf;
		char* e = minbuf + rd;
		char* q;

		while((q = findline(p, e))) {
			*q = '\0';
			scanline(p, opts);
			p = q + 1;
		} if(p < e) {
			of = e - p;
			memcpy(minbuf, p, of);
		}
	}
}

#define OPTS ""

int main(int argc, char** argv)
{
	int opts = 0;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	wrheader();

	if(i < argc) while(i < argc)
		reportfs(argv[i++], "", opts);
	else
		scanall(opts);

	flushout();

	return 0;
}
