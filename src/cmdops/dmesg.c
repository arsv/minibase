#include <sys/klog.h>
#include <sys/mmap.h>

#include <alloca.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <util.h>
#include <fail.h>

/* This is a simple dmesg utility, right?
   Well nope. It's not that simple, and there's a lot of uncertainity
   in what should have been a simple straightforward tool.

   Most of the stuff below, including output buffering, deals with
   formatting timestamps and colorizing the output.
   TODO: throw it all out.
   (on the other hand, there's always -r which skips it)

   No support for -s (set userspace buffer size), because why would
   anyone want to get the last N *bytes* from the kernel log?
   N lines would make sense, yeah, but not bytes. */

#define OPTS "acnr"
#define OPT_a (1<<0)	/* no ansi colors */
#define OPT_c (1<<1)	/* clear kernel ring buffer */
#define OPT_n (1<<2)	/* set log level instead of reading KRB */
#define OPT_r (1<<3)	/* dump raw KRB contents, no processing */

#define OUTBUF (4*1024)
#define MINLOGBUF (16*1024)
#define MAXLOGBUF (16*1024*1024)

ERRTAG = "dmesg";
ERRLIST = {
	REPORT(EINVAL), REPORT(EFAULT), REPORT(ENOSYS), REPORT(EPERM),
	RESTASNUMBERS
};

/* We could have put logbuf and outbuf to .bss and it would work
   well given MAXLOGBUF is known, but only for MMU systems.
   On non-MMU systems blindly allocating 16M is a no-go.

   This is just dmesg, no big deal really performance-wise,
   so let's keep it NOMMU friendly and use mmap here.
 
   The buffer is never explicitly unmmaped since prettyprint()
   is only ever called once and we exit immediately after that. */

static char* mmapbuf(long len)
{
	const int prot = PROT_READ | PROT_WRITE;
	const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	long ptr = sys_mmap(NULL, len, prot, flags, -1, 0);

	if(mmap_error(ptr))
		fail("mmap", NULL, ptr);
	else
		return (char*)ptr;
}

static void mmapbufout(struct bufout* bo, int fd, int len)
{
	bo->fd = fd;
	bo->len = len;
	bo->ptr = 0;
	bo->buf = mmapbuf(len);
}

static void xbufout(struct bufout* bo, const char* buf, int len)
{
	xchk(bufout(bo, (char*)buf, len), "write failed", NULL);
}

/* Output colors */

#define COL_RESET 0
#define COL_TIME  1
#define COL_TAG   2
#define COL_ERR   3
#define COL_WARN  4
#define NCOLORS 5

#define CSI "\033["

static const struct {
	unsigned char len;
	char cmd[8];
} colors[NCOLORS] = {
	{ 4, CSI "0m" },
	{ 5, CSI "32m" },
	{ 5, CSI "33m" },
	{ 5, CSI "31m" },
	{ 6, CSI "0;1m" }
};

static void xbufcolor(struct bufout* bo, int opts, int col)
{
	if(opts & OPT_a)
		return;
	if(col < 0 || col >= NCOLORS)
		return;

	xbufout(bo, colors[col].cmd, colors[col].len);
}

static int prioclr(int prio)
{
	prio &= 0x07;

	if(prio < 0)
		return COL_RESET;
	if(prio < 3)
		return COL_ERR;
	if(prio < 6)
		return COL_WARN;
	return COL_RESET;
}

/* The raw klog format is not exactly human-friendly, so we parse it,
   convert prio and ts to something readable and also maybe colorize
   tag/msg depending on prio.

   prio               tag
   vvv                vvvvvv
   <6>[573491.278754] IPv6: ADDRCONF(NETDEV_UP): wlp9s0: link is not ready
       ^^^^^^^^^^^^^        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
            ts              msg */

struct logmsg {
	int prio;
	long ts;
	char* tag;
	char* msg;
	char* end;
};

static char* parseprio(char* str, int* prio)
{
	char* p = str;

	if(*p != '<')
		return NULL;
	if(!(p = parseint(p + 1, prio)) || (*p != '>'))
		return NULL;

	return p + 1;
};

static char* parsetime(char* str, long* ts)
{
	char* p = str;
	long nsec;

	if(*p++ != '[')
		return NULL;
	while(*p == ' ') /* leading pad in ts: [   123.34546456] */
		p++;

	if(!(p = parselong(p, ts)))
		return NULL;

	if(*p == ']')    /* no nsec part: [ 23453] */
		return p + 1;
	else if(*p != '.')
		return NULL;

	if(!(p = parselong(p + 1, &nsec))) /* parsed and discarded */
		return NULL;

	if(*p == ']')
		return p + 1;
	else
		return NULL;
}

static int parseline(char* str, char* end, struct logmsg* m)
{
	char* p = str;
	char* q;

	if(!(p = parseprio(p, &(m->prio))))
		return -1;
	if(!(p = parsetime(p, &(m->ts))))
		return -1;
	if(*p == ' ') p++;

	if((q = strstr(p, ": "))) {
		m->tag = p;
		p = q + 2;
	} else {
		m->tag = NULL;
	};

	m->msg = p;
	m->end = end;

	return 0;
}

/* This pretty gross routine makes something like "[  1243]".
   Could have been a call to fmtint or something, but most of the code
   here deals with padding so that would only make things worse.

   pad -vvvv
       [    123455]
            ^^^^^^- len

   The stamp is padded to 6 internal chars, and we assume size
   is at least 8. */

static int numlen(long num)
{
	int len = 1;
	while(num >= 10) { len++; num /= 10; }
	return len;
}

/* Time handling in dmesg is fscked up beyond any reasonable limits.
   The timestamps should follow CLOCK_MONOTONIC, probably, but on my system
   atm they just happen to be ~600s (10 minutes) ahead.

   There are no obvious sources to get adjustment for this difference.

   I would much rather prefer something like "n seconds ago" as a timing
   format for a ring buffer, but alas, it just does not work.
   The tool started doing exactly that, diffing the stamps against
   CLOCK_MONOTONIC, but that's unreliable and will only confuse users.

   For the same reason there are no attempt to implement -T option
   (human-readable wall clock times).

   The attempts to do something with the timestamps is the only reason
   for parsing them there. As it is now, it would actually be easier
   to do strcpy or something along those lines, but I hope to fix it
   at some point. */

static int formattime(char* buf, int size, long dt)
{
	int dw = numlen(dt);

	int pad = 6 - dw; if(pad < 0) pad = 0;

	int rem = size - 3 - pad; /* space remaining for len */
	int len = rem < dw ? rem : dw;

	int i;
	for(i = 1; i < 1 + pad; i++)
		buf[i] = ' ';
	for(i = 0; i < dw; i++, dt /= 10)
		if(i < len)
			buf[1+pad+(len-i-1)] = '0' + (dt % 10);

	buf[0] = '['; buf[1+pad+len] = ']';

	return pad + len + 2;
}

static void formatline(struct bufout* bo, struct logmsg* m, int opts)
{
	long ts = m->ts;

	char timebuf[20];
	int timelen = formattime(timebuf, sizeof(timebuf), ts);

	xbufcolor(bo, opts, COL_TIME);
	xbufout(bo, timebuf, timelen);
	xbufout(bo, " ", 1);

	if(m->tag) {
		xbufcolor(bo, opts, COL_TAG);
		xbufout(bo, m->tag, m->msg - m->tag);
	}

	int pc = prioclr(m->prio);
	xbufcolor(bo, opts, pc);
	xbufout(bo, m->msg, m->end - m->msg);
	if(pc != COL_RESET) xbufcolor(bo, opts, COL_RESET);
	xbufout(bo, "\n", 1);
}

/* Lines we could not parse we just print raw */

static void writeraw(struct bufout* bo, char* p, char* e)
{
	xbufout(bo, p, e - p);
	xbufout(bo, "\n", 1);
}

static void prettyprint(char* logbuf, int loglen, int opts)
{
	struct bufout bo;
	char* logend = logbuf + loglen;

	char* p = logbuf;	/* start of string */
	char* e;		/* end of string */
	struct logmsg m;	/* parsed line */

	mmapbufout(&bo, 1, OUTBUF);

	while(p < logend) {
		e = strqbrk(p, "\n"); *e = '\0';

		if(!parseline(p, e, &m))
			formatline(&bo, &m, opts);
		else
			writeraw(&bo, p, e);

		p = e + 1;
	}

	xchk(bufoutflush(&bo), "write", NULL);
}

static void showklogbuf(int opts)
{
	long len = xchk(sys_klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0),
			"cannot get klog buffer size", NULL);

	if(len < MINLOGBUF)
		len = MINLOGBUF;
	if(len > MAXLOGBUF - 1)
		len = MAXLOGBUF - 1;

	char* logbuf = mmapbuf(len + 1);

	int act = (opts & OPT_c) ? SYSLOG_ACTION_READ_CLEAR
	                         : SYSLOG_ACTION_READ_ALL;
	
	len = xchk(sys_klogctl(act, logbuf, len),
		"cannot read klog buffer", NULL);

	if(len > 0 && logbuf[len-1] != '\n')
		logbuf[len++] = '\n';

	if(opts & OPT_r)
		xchk(writeall(1, logbuf, len), "write failed", NULL);
	else
		prettyprint(logbuf, len, opts);
}

static void setkloglevel(int lvl)
{
	xchk(sys_klogctl(SYSLOG_ACTION_CONSOLE_LEVEL, NULL, lvl),
		"cannot set loglevel", NULL);
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
	unsigned int lvl = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(i < argc && (opts & OPT_n))
		lvl = xatou(argv[i++]);
	else if(opts & OPT_n)
		fail("missing numeric argument", NULL, 0);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	if(opts & OPT_n)
		setkloglevel(lvl);
	else
		showklogbuf(opts);

	return 0;
}
