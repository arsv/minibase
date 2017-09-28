#include <sys/file.h>
#include <sys/mman.h>
#include <sys/inotify.h>

#include <errtag.h>
#include <string.h>
#include <format.h>
#include <time.h>
#include <util.h>

#include "common.h"

ERRTAG("logcat");

#define TAGSPACE 14

#define OPTS "cbfa"
#define OPT_c (1<<0)	/* no color */
#define OPT_b (1<<1)	/* both current and old (rotated) log */
#define OPT_f (1<<2)	/* follow */
#define OPT_a (1<<3)	/* all lines, not just N last */
#define SET_i (1<<10)	/* ignore errors */

struct buf {
	char* brk;
	char* ptr;
	char* end;
};

struct top {
	int opts;
	char* tag;
	int tlen;

	int fd;
	struct buf buf;
	struct buf out;
	int over;

	char* name;
	char* base;

	int in;
	int inf;
	int ind;
};

#define CTX struct top* ctx

/* Buffered output (may be a circular buffer depending on opts) */

static void write(char* buf, char* end)
{
	if(buf >= end) return;
	writeall(STDOUT, buf, end - buf);
}

static void output(CTX, char* str, int len)
{
	char* brk = ctx->out.brk;
	char* ptr = ctx->out.ptr;
	char* end = ctx->out.end;
	int all = ctx->opts & OPT_a;

	while(len > 0) {
		int left = end - ptr;
		int run = len < left ? len : left;

		if(!run) break;

		memcpy(ptr, str, run);
		str += run;
		len -= run;

		ptr += run;

		if(ptr < end) break;

		if(all)
			write(brk, ptr);
		else
			ctx->over = 1;

		ptr = brk;
	}

	ctx->out.ptr = ptr;
}

static void flushout(CTX)
{
	char* brk = ctx->out.brk;
	char* ptr = ctx->out.ptr;
	char* end = ctx->out.end;
	char* e;

	if(ctx->over) {
		if((e = strecbrk(ptr, end, '\n')) < end) {
			write(e + 1, end);
			write(brk, ptr);
		} else if((e = strecbrk(brk, ptr, '\n')) < ptr) {
			write(e + 1, ptr);
		}
	} else {
		write(brk, ptr);
	}

	ctx->out.ptr = brk;
	ctx->over = 0;
}

/* Formatting routines */

static int tagged(CTX, char* ls, char* le)
{
	char* tag = ctx->tag;
	int tlen = ctx->tlen;

	if(!tag)
		return 1;

	if(le - ls < TAGSPACE + tlen + 1)
		return 0;
	if(strncmp(ls + TAGSPACE, tag, tlen))
		return 0;
	if(ls[TAGSPACE+tlen] != ':')
		return 0;

	return 1;
}

static char* color(char* p, char* e, int opts, int a, int b)
{
	if(opts & OPT_c)
		return p;

	p = fmtstr(p, e, "\033[");
	p = fmtint(p, e, a);
	p = fmtstr(p, e, ";");
	p = fmtint(p, e, b);
	p = fmtstr(p, e, "m");

	return p;
}

static char* reset(char* p, char* e, int opts)
{
	if(opts & OPT_c)
		return p;

	p = fmtstr(p, e, "\033[0m");

	return p;
}

static void outcolor(CTX, int a, int b)
{
	int opts = ctx->opts;

	if(opts & OPT_c) return;

	FMTBUF(p, e, buf, 10);
	p = color(p, e, opts, a, b);
	FMTEND(p, e);

	output(ctx, buf, p - buf);
}

static void outreset(CTX)
{
	if(ctx->opts & OPT_c) return;

	output(ctx, "\033[0m", 4);
}

static char* skip_prefix(char* ls, char* le)
{
	char* p;

	for(p = ls; p < le - 2; p++)
		if(*p == ':' && *(p+1) == ' ')
			return p;

	return NULL;
}

static void format(CTX, uint64_t ts, int prio, char* ls, char* le)
{
	int opts = ctx->opts;
	struct timeval tv = { ts, 0 };
	struct tm tm;
	char* sep;

	tv2tm(&tv, &tm);

	FMTBUF(p, e, buf, 50 + le - ls + 50);
	p = color(p, e, opts, 0, 32);
	p = fmttm(p, e, &tm);
	p = reset(p, e, opts);
	p = fmtstr(p, e, opts & OPT_c ? "  " : " ");
	FMTEND(p, e);

	output(ctx, buf, p - buf);

	if((sep = skip_prefix(ls, le))) {
		outcolor(ctx, 0, 33);
		output(ctx, ls, sep - ls);
		outreset(ctx);
		ls = sep;
	}

	if(prio < 2)
		outcolor(ctx, 1, 31);
	else if(prio < 4)
		outcolor(ctx, 1, 37);

	output(ctx, ls, le - ls);

	if(prio < 4)
		outreset(ctx);

	output(ctx, "\n", 1);
}

/* (ls, le) pair is *not* 0-terminated!
   parseu64 however expects a 0-terminated string.

   le points to \n if we're lucky, or past the end of buffer if not.

   The lines look like this:

       1503700695 6 foo: some text goes here

   Our goal here is to grab timestamp and priority to format them later.
   Current syslogd should always leave exactly TAGSPACE characters before
   the "foo: ..." part but we allow for less. */

void process(CTX, char* ls, char* le)
{
	int ln = le - ls;
	int tl = ln < TAGSPACE ? ln : TAGSPACE;
	int prio;
	char pref[tl+1];
	uint64_t ts;
	char* p;

	memcpy(pref, ls, tl);
	pref[tl] = '\0';

	if(!(p = parseu64(pref, &ts)) || *p++ != ' ')
		return;

	p++; /* skip facility */

	if(*p < '0' || *p > '9')
		return;

	prio = *p++ - '0';

	if(*p++ != ' ')
		return;

	int plen = p - pref;

	format(ctx, ts, prio, ls + plen, le);
}

/* Grep mode. For simplicity, mmap the whole file into memory.

   Additional care is needed for rotated logs: if -b (both) is specified,
   we do the equivalent of "grep tag sysold syslog" instead of just
   "grep tag syslog". */

static int mmap_whole(CTX, char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		return fd;
	if((ret = sys_fstat(fd, &st)) < 0)
		return fd;

	uint64_t size = st.size;

	void* ptr = sys_mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

	if(mmap_error(ptr))
		fail("mmap", name, (long)ptr);

	ctx->buf.brk = ptr;
	ctx->buf.ptr = ptr + size;
	ctx->buf.end = ptr + size;

	ctx->name = name;
	ctx->base = NULL;

	return 0;
}

void dump_logfile(CTX, char* name)
{
	int opts = ctx->opts;
	char *ls, *le;
	int ret;

	if((ret = mmap_whole(ctx, name)) < 0) {
		if(opts & SET_i)
			return;
		fail(NULL, name, ret);
	}

	char* buf = ctx->buf.brk;
	char* end = ctx->buf.ptr;

	for(ls = buf; ls < end; ls = le + 1) {
		le = strecbrk(ls, end, '\n');

		if(!tagged(ctx, ls, le))
			continue;

		process(ctx, ls, le);
	}
}

static void dump_logs(CTX)
{
	if(!(ctx->opts & OPT_b))
		goto curr;

	ctx->opts |= SET_i;
	dump_logfile(ctx, OLDLOG);
	ctx->opts &= ~SET_i;
curr:
	dump_logfile(ctx, VARLOG);
}

/* Follow mode, uses inotify to watch /var/log/syslog for updates.
   In addition to the syslog, we watch to directory to catch possible
   log rotation by syslogd.

   Note that despite the looks, inotify_add_watch IN_MODIFY tracks inodes,
   not file names, so we have to change the watch when the file gets changed.
   The whole thing reeks of race conditions, but there's apparently no
   inotify_add_fd call or anything resembling it. */

static int start_inotify(CTX)
{
	int fd, ret;
	char* name = ctx->name;

	if((fd = sys_inotify_init()) < 0)
		fail("inotify-start", NULL, fd);

	if((ret = sys_inotify_add_watch(fd, name, IN_MODIFY)) < 0)
		fail("inotify-add", name, ret);

	ctx->in = fd;
	ctx->inf = ret;

	char* dir = LOGDIR;

	if((ret = sys_inotify_add_watch(fd, dir, IN_CREATE)) < 0)
		fail("inotify-add", dir, ret);

	ctx->ind = ret;

	return fd;
}

static int wait_inotify(CTX)
{
	char buf[500];
	int newfile = 0;
	int rd;

	if((rd = sys_read(ctx->in, buf, sizeof(buf))) < 0)
		fail("inotify", NULL, rd);

	void* p = buf;
	void* e = buf + rd;

	while(p < e) {
		struct inotify_event* evt = p;

		if(evt->mask != IN_CREATE)
			;
		else if(!strcmp(evt->name, ctx->base))
			newfile = 1;

		p += sizeof(*evt) + evt->len;
	}

	return newfile;
}

static void alloc_bufs(CTX)
{
	const int outlen = PAGE;
	const int buflen = 2*PAGE;

	int tail = ctx->opts & OPT_f;
	int size = tail ? outlen + buflen : outlen;

	char* brk = sys_brk(0);
	char* end = sys_brk(brk + size);

	if(brk_error(brk, end))
		fail("cannot allocate memory", NULL, 0);

	ctx->out.brk = brk;
	ctx->out.ptr = brk;
	ctx->out.end = brk + outlen;

	if(!tail) return;

	brk += outlen;
	ctx->buf.brk = brk;
	ctx->buf.ptr = brk;
	ctx->buf.end = end;
}

static void open_logfile(CTX, char* name)
{
	int fd;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	ctx->fd = fd;
	ctx->name = name;
	ctx->base = basename(name);
}

static void reopen_logfile(CTX)
{
	int ret;
	char* name = ctx->name;
	int in = ctx->in;
	int inf = ctx->inf;
	int fd = ctx->fd;

	sys_inotify_rm_watch(in, inf);

	sys_close(fd);
	open_logfile(ctx, name);

	if((ret = sys_inotify_add_watch(in, name, IN_MODIFY)) < 0)
		fail("inotify-add", name, ret);

	ctx->inf = ret;
}

static char* find_line_end(CTX, char* from)
{
	char* p = from;
	char* e = ctx->buf.ptr;

	for(; p < e; p++)
		if(*p == '\n')
			return p;

	return NULL;
}

static int read_chunk(CTX)
{
	char* end = ctx->buf.end;
	char* ptr = ctx->buf.ptr;
	long left = end - ptr;
	int rd, fd = ctx->fd;

	if((rd = sys_read(fd, ptr, left)) < 0)
		fail("read", ctx->name, rd);

	ctx->buf.ptr += rd;

	return rd;
}

static void shift_chunk(CTX, char* p)
{
	char* brk = ctx->buf.brk;
	char* ptr = ctx->buf.ptr;
	char* end = ctx->buf.end;

	if(p < brk || p > ptr)
		return;

	long left = ptr - p;

	memmove(brk, p, left);
	ctx->buf.ptr = brk + left;

	if(ptr < end) *ptr = '\0'; /* for debugging convenience */
}

static void seek_to_start_of_line(CTX)
{
	struct stat st;
	int ret, fd = ctx->fd;
	char* end = ctx->buf.end;
	char* ptr = ctx->buf.ptr;
	long chunk = end - ptr;
	char* ls;

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", ctx->name, ret);

	if(!st.size)
		return;
	if(st.size < chunk)
		;
	else if((ret = sys_lseek(fd, st.size - chunk, SEEK_SET)) < 0)
		fail("seek", ctx->name, ret);

	read_chunk(ctx);

	if(!(ls = find_line_end(ctx, ctx->buf.brk)))
		fail("corrupt logfile", NULL, 0);

	shift_chunk(ctx, ls + 1);
}

static void process_chunk(CTX)
{
	char* buf = ctx->buf.brk;
	char *ls, *le;

	for(ls = buf; (le = find_line_end(ctx, ls)); ls = le + 1) {
		if(le - ls < TAGSPACE)
			continue;
		if(!tagged(ctx, ls, le))
			continue;

		process(ctx, ls, le);
	}

	shift_chunk(ctx, ls);
}

static void slurp_tail(CTX)
{
	int rd;

	while((rd = read_chunk(ctx)))
		process_chunk(ctx);
}

static void flushbuf(CTX)
{
	/* silently drop partial line that may be there */
	ctx->buf.ptr = ctx->buf.brk;
}

static void follow_log(CTX)
{
	char* name = VARLOG;
	int opts = ctx->opts;

	open_logfile(ctx, name);
	start_inotify(ctx);

	if(!(opts & OPT_a)) {
		seek_to_start_of_line(ctx);
		process_chunk(ctx);
	} else {
		slurp_tail(ctx);
		ctx->opts &= ~OPT_a; /* suppress output wrap */
	}

	while(1) {
		flushout(ctx);

		int nf = wait_inotify(ctx);

		slurp_tail(ctx);

		if(!nf) continue;

		flushbuf(ctx);
		reopen_logfile(ctx);
		slurp_tail(ctx);
	}
}

/* -- */

int main(int argc, char** argv)
{
	int i = 1;
	char* tag = NULL;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc)
		tag = argv[i++];
	if(i < argc)
		fail("too many arguments", NULL, 0);

	struct top ctx;
	memzero(&ctx, sizeof(ctx));

	ctx.opts = opts;
	ctx.tag = tag;
	ctx.tlen = tag ? strlen(tag) : 0;

	alloc_bufs(&ctx);

	if(opts & OPT_f)
		follow_log(&ctx);
	else
		dump_logs(&ctx);

	flushout(&ctx);

	return 0;
}
