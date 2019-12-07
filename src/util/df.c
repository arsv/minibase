#include <bits/major.h>
#include <sys/file.h>
#include <sys/statfs.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <util.h>
#include <main.h>

#define OPTS "am"
#define OPT_a (1<<0)	/* show all mounted filesystems */
#define OPT_m (1<<1)	/* show in-memory filesystems (dev id 0:*) */
#define SET_x (1<<16)	/* show systems with zero block count */

ERRTAG("df");

struct top {
	struct bufout bo;
	char* statfile;
	char* statdev;
	int opts;
};

#define CTX struct top* ctx

char minbuf[4096]; /* mountinfo */
char outbuf[4096];

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
	unsigned sfi = muinkb ? 1 : 0;
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

static void output(CTX, const char* buf, int len)
{
	bufout(&ctx->bo, (char*)buf, len);
}

static void outstr(CTX, const char* str)
{
	output(ctx, str, strlen(str));
}

/* The output should look like something like this:

   Size   Used    Free         Mountpoint
   9.8G   4.5G    5.3G   47%   /

   The numbers should be aligned on decimal point. */

static void init_output(CTX)
{
	static const char hdr[] =
	    /* |1234.1x_| */
	       "   Size "
	       "   Used "
	       "   Free"
	    /* |    12%   | */
	       "    Use   "
	       "Mountpoint\n";

	struct bufout* bo = &ctx->bo;

	bo->fd = STDOUT;
	bo->buf = outbuf;
	bo->ptr = 0;
	bo->len = sizeof(outbuf);

	output(ctx, hdr, sizeof(hdr));
}

static void fini_output(CTX)
{
	bufoutflush(&ctx->bo);
}

static char* fmtstatfs(char* p, char* e, struct statfs* st)
{
	long bs = st->bsize;
	long blocksused = st->blocks - st->bavail;
	long blockstotal = st->blocks;

	p = fmtmem(p, e, st->blocks, bs);
	p = fmtstr(p, e, " ");
	p = fmtmem(p, e, st->blocks - st->bavail, bs);
	p = fmtstr(p, e, " ");
	p = fmtmem(p, e, st->bavail, bs);
	p = fmtstr(p, e, "   ");

	if(blockstotal) {
		long perc = 100*blocksused/blockstotal;
		char* q = p;
		p = fmtlong(p, e, perc);
		p = fmtstr(p, e, "%");
		p = fmtpad(q, e, 4, p);
	} else {
		p = fmtstr(p, e, "  --");
	}

	p = fmtstr(p, e, "   ");

	return p;
}

/* When reporting an explicitly named file, we call statfs() on its
   mountpoint found in mountinfo. This is counter-intuitive, but ensures
   the output lines are consistent (i.e. the numbers do in fact correspond
   to the mountpoint shown). */

static void reportfs(CTX, char* mountpoint)
{
	struct statfs st;
	int opts = ctx->opts;
	char* file = mountpoint ? mountpoint : ctx->statfile;
	int ret;

	if((ret = sys_statfs(file, &st)) < 0)
		fail("statfs", file, ret);
	if(!st.blocks && !(opts & SET_x))
		return;

	FMTBUF(p, e, buf, 100);
	p = fmtstatfs(p, e, &st);
	FMTEND(p, e);

	output(ctx, buf, p - buf);

	if(!mountpoint)
		outstr(ctx, "? ");

	outstr(ctx, file);

	outstr(ctx, "\n");
}

/* Device major 0 means virtual. Used/free space counts are meaningless
   for most of them, so they are skipped unless we've got -m or -a.

   With -m or -a, there's further distinction between in-memory filesystems
   like tmpfs which do have some notion of size and free space,
   and stuff that's completely virtual like cgroups fs and such.
   Those are filtered out in reportfs on f_blocks = 0. */

static int skipdev(CTX, char* dev)
{
	char* statdev = ctx->statdev;
	int opts = ctx->opts;
	int nodev = (dev[0] == '0' && dev[1] == ':');

	if(statdev)
		return strcmp(dev, statdev);
	if(nodev && !(opts & (OPT_a | OPT_m)))
		return 1;
	if(!nodev && (opts & OPT_m))
		return 1;

	return 0;
}

/* The data is reported by reading /proc/self/mountinfo linewise,
   filtering interesting lines, and calling reportfs() which in turn
   calls statfs(2).

   A line from mountinfo looks like this:

   66 21 8:3 / /home rw,noatime,nodiratime shared:25 - ext4 /dev/sda3 rw,stripe=128,data=ordered

   We need fields [2] devid, [4] mountpoint, and possibly [8] device.
   The fields are presumed to be space-separated, with no spaces within.
   If they aren't, we'll get garbage.

   Actually, we don't need [8] device. Too far to the right.
   Let's take 5 leftmost fields and be done with that. */

#define MP 5

static char* findline(char* p, char* e)
{
	while(p < e)
		if(*p == '\n')
			return p;
		else
			p++;
	return NULL;
}

static int splitline(char* line, char** parts, int n)
{
	char* p = line;
	char* q;
	int i;

	for(i = 0; i < n; i++) {
		if(!*(q = strcbrk(p, ' ')))
			break;
		parts[i] = p;
		*q = '\0'; p = q + 1;
	};

	return (i < n);
}

static void scanall(CTX)
{
	const char* name = "/proc/self/mountinfo";
	long fd, rd, off = 0;
	char* buf = minbuf;
	int buflen = sizeof(minbuf);
	char* mp[MP];

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	while((rd = sys_read(fd, buf + off, buflen - off)) > 0) {
		char* p = minbuf;
		char* e = minbuf + rd;
		char* q;

		while((q = findline(p, e))) {
			char* line = p; *q = '\0'; p = q + 1;

			if(splitline(line, mp, MP))
				continue;

			char* linedev = mp[2];
			char* linemp = mp[4];

			if(skipdev(ctx, linedev))
				continue;

			reportfs(ctx, linemp);

			if(ctx->statdev)
				goto done;
		} if(p < e) {
			off = e - p;
			memcpy(buf, p, off);
		}

		if(rd < buflen - off) break;
	}
	/* specific file was given but we could not find the mountpoint */
	if(ctx->statdev)
		reportfs(ctx, NULL);
done:
	sys_close(fd);
}

/* statfs(file) provides all the data needed except for mountpoint, which
   we've got to fish out of /proc/self/mountinfo using device maj:min
   reported by stat(fs) as the key.

   This only applies to explicit file arguments of course. */

static char* fmtdev(char* p, char* e, uint64_t dev)
{
	int min = minor(dev);
	int maj = major(dev);

	p = fmti32(p, e, maj);
	p = fmtstr(p, e, ":");
	p = fmti32(p, e, min);

	return p;
}

static void scan(CTX, char* statfile)
{
	int ret;
	struct stat st;

	if((ret = sys_stat(statfile, &st)) < 0)
		fail(NULL, statfile, ret);

	FMTBUF(p, e, buf, 20);
	p = fmtdev(p, e, st.dev);
	FMTEND(p, e);

	ctx->opts |= SET_x;
	ctx->statfile = statfile;
	ctx->statdev = buf;

	scanall(ctx);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	int i = 1;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		ctx->opts = argbits(OPTS, argv[i++] + 1);

	init_output(ctx);

	if(i >= argc)
		scanall(ctx);
	else for(; i < argc; i++)
		scan(ctx, argv[i]);

	fini_output(ctx);

	return 0;
}
