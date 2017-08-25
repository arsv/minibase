#include <sys/file.h>
#include <sys/mman.h>
#include <sys/inotify.h>

#include <errtag.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <time.h>
#include <util.h>

#include "common.h"

ERRTAG("logcat");

#define TAGSPACE 13

#define OPTS "cbf"
#define OPT_c (1<<0)	/* no color */
#define OPT_b (1<<1)	/* both current and old (rotated) log */
#define OPT_f (1<<2)	/* follow */
#define SET_i (1<<10)	/* ignore errors */

struct mbuf {
	char* ptr;
	char* end;
};

struct tail {
	int fd;
	char* brk;
	char* ptr;
	char* end;
	int len;
	char* name;
	char* base;

	int in;
	int inf;
	int ind;
};

struct grep {
	char* tag;
	int tlen;
	int opts;
};

/* Formatting routines */

static int tagged(char* ls, char* le, struct grep* gr)
{
	char* tag = gr->tag;
	int tlen = gr->tlen;

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

static void outcolor(int opts, int a, int b)
{
	if(opts & OPT_c) return;

	FMTBUF(p, e, buf, 10);
	p = color(p, e, opts, a, b);
	FMTEND(p, e);

	writeout(buf, p - buf);
}

static void outreset(int opts)
{
	if(opts & OPT_c) return;

	writeout("\033[0m", 4);
}

static char* skip_prefix(char* ls, char* le)
{
	char* p;

	for(p = ls; p < le; p++)
		if(*p == ' ')
			break;
		else if(*p == ':')
			return p + 1;

	return NULL;
}

static void format(uint64_t ts, int prio, char* ls, char* le, int opts)
{
	struct timeval tv = { ts, 0 };
	struct tm tm;
	char* sep;

	tv2tm(&tv, &tm);

	FMTBUF(p, e, buf, 50);
	p = color(p, e, opts, 0, 32);
	p = fmttm(p, e, &tm);
	p = reset(p, e, opts);
	p = fmtstr(p, e, opts & OPT_c ? "  " : " ");
	FMTEND(p, e);

	writeout(buf, p - buf);

	if((sep = skip_prefix(ls, le))) {
		outcolor(opts, 0, 33);
		writeout(ls, sep - ls);
		outreset(opts);
		ls = sep;
	}

	if(prio < 2)
		outcolor(opts, 1, 31);
	else if(prio < 4)
		outcolor(opts, 1, 37);

	writeout(ls, le - ls);

	if(prio < 4)
		outreset(opts);

	writeout("\n", 1);
}

/* (ls, le) pair is *not* 0-terminated!
   parseu64 however expects a 0-terminated string.

   le points to \n if we're lucky, or past the end of buffer if not.

   The lines look like this:

       1503700695 6 foo: some text goes here

   Our goal here is to grab timestamp and priority to format them later.
   Current syslogd should always leave exactly TAGSPACE characters before
   the "foo: ..." part but we allow for less. */

void process(char* ls, char* le, int opts)
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
	if(*p < '0' || *p > '9')
		return;

	prio = *p++ - '0';

	if(*p++ != ' ')
		return;

	int plen = p - pref;

	format(ts, prio, ls + plen, le, opts);
}

/* Grep mode. For simplicity, mmap the whole file into memory.

   Additional care is needed for rotated logs: if -b (both) is specified,
   we do the equivalent of "grep tag sysold syslog" instead of just
   "grep tag syslog". */

static int mmap_whole(struct mbuf* mb, char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		return fd;
	if((ret = sys_fstat(fd, &st)) < 0)
		return fd;

	uint64_t size = st.size;

	long ptr = sys_mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

	if(mmap_error(ptr))
		fail("mmap", name, ptr);

	mb->ptr = (void*)ptr;
	mb->end = mb->ptr + size;

	return 0;
}

void dump_logfile(char* name, char* tag, int opts)
{
	struct grep gr = {
		.tag = tag,
		.tlen = tag ? strlen(tag) : 0,
		.opts = opts
	};

	struct mbuf mb;
	char *ls, *le;
	int ret;

	if((ret = mmap_whole(&mb, name)) < 0) {
		if(opts & SET_i)
			return;
		fail(NULL, name, ret);
	}

	for(ls = mb.ptr; ls < mb.end; ls = le + 1) {
		le = strecbrk(ls, mb.end, '\n');

		if(!tagged(ls, le, &gr))
			continue;

		process(ls, le, opts);
	}
}

static void dump_logs(char* tag, int opts)
{
	if(opts & OPT_b)
		dump_logfile(OLDLOG, tag, opts | SET_i);

	dump_logfile(VARLOG, tag, opts);
}

/* Follow mode, uses inotify to watch /var/log/syslog for updates.
   In addition to the syslog, we watch to directory to catch possible
   log rotation by syslogd.

   Note that despite the looks, inotify_add_watch IN_MODIFY tracks inodes,
   not file names, so we have to change the watch when the file gets changed.
   The whole thing reeks of race conditions, but there's apparently no
   inotify_add_fd call or anything resembling it. */

static int start_inotify(struct tail* ta)
{
	int fd, ret;
	char* name = ta->name;

	ta->base = (char*)basename(name);

	if((fd = sys_inotify_init()) < 0)
		fail("inotify-start", NULL, fd);

	if((ret = sys_inotify_add_watch(fd, name, IN_MODIFY)) < 0)
		fail("inotify-add", name, ret);

	ta->in = fd;
	ta->inf = ret;

	char* dir = LOGDIR;

	if((ret = sys_inotify_add_watch(fd, dir, IN_CREATE)) < 0)
		fail("inotify-add", dir, ret);

	ta->ind = ret;

	return fd;
}

static int wait_inotify(struct tail* ta)
{
	char buf[500];
	int newfile = 0;
	int rd;

	if((rd = sys_read(ta->in, buf, sizeof(buf))) < 0)
		fail("inotify", NULL, rd);

	void* p = buf;
	void* e = buf + rd;

	while(p < e) {
		struct inotify_event* evt = p;

		if(evt->mask != IN_CREATE)
			;
		else if(!strcmp(evt->name, ta->base))
			newfile = 1;

		p += sizeof(*evt) + evt->len;
	}

	return newfile;
}

static void alloc_tail_buf(struct tail* ta, int size)
{
	char* brk = (void*)sys_brk(0);
	char* end = (void*)sys_brk(brk + size);

	if(end <= brk)
		fail("cannot allocate memory", NULL, 0);

	ta->brk = brk;
	ta->ptr = brk;
	ta->end = end;
}

static void open_logfile(struct tail* ta, char* name)
{
	int fd;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	ta->fd = fd;
	ta->name = name;
}

static void reopen_logfile(struct tail* ta)
{
	int ret;
	char* name = ta->name;
	int in = ta->in;

	sys_inotify_rm_watch(in, ta->inf);

	sys_close(ta->fd);
	open_logfile(ta, name);

	if((ret = sys_inotify_add_watch(in, name, IN_MODIFY)) < 0)
		fail("inotify-add", name, ret);

	ta->inf = ret;
}

static char* find_line_end(struct tail* ta, char* from)
{
	char* p;

	for(p = from; p < ta->ptr; p++)
		if(*p == '\n')
			return p;

	return NULL;
}

static int read_chunk(struct tail* ta)
{
	long left = ta->end - ta->ptr;
	char* ptr = ta->ptr;
	int rd;

	if((rd = sys_read(ta->fd, ptr, left)) < 0)
		fail("read", ta->name, rd);

	ta->ptr += rd;

	return rd;
}

static void shift_chunk(struct tail* ta, char* p)
{
	if(p < ta->brk || p > ta->ptr)
		return;

	long left = ta->ptr - p;

	memmove(ta->brk, p, left);
	ta->ptr = ta->brk + left;

	if(ta->ptr < ta->end)
		*(ta->ptr) = '\0'; /* for debugging convenience */
}

static void seek_to_start_of_line(struct tail* ta)
{
	struct stat st;
	int ret, fd = ta->fd;
	long chunk = ta->end - ta->ptr;
	char* ls;

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", ta->name, ret);

	if(!st.size)
		return;
	if(st.size < chunk)
		;
	else if((ret = sys_lseek(fd, st.size - chunk, SEEK_SET)) < 0)
		fail("seek", ta->name, ret);

	read_chunk(ta);

	if(!(ls = find_line_end(ta, ta->brk)))
		fail("corrupt logfile", NULL, 0);

	shift_chunk(ta, ls + 1);
}

static void process_chunk(struct tail* ta, struct grep* gr)
{
	char* brk = ta->brk;
	char *ls, *le;

	for(ls = brk; (le = find_line_end(ta, ls)); ls = le + 1) {
		if(le - ls < TAGSPACE)
			continue;
		if(!tagged(ls, le, gr))
			continue;

		process(ls, le, gr->opts);
	}

	shift_chunk(ta, ls);
}

static void slurp_tail(struct tail* ta, struct grep* gr)
{
	int rd;

	while((rd = read_chunk(ta)))
		process_chunk(ta, gr);
}

static void flush_buf(struct tail* ta)
{
	/* silently drop partial line that may be there */
	ta->ptr = ta->brk;
}

static void follow_log(char* tag, int opts)
{
	struct grep gr = {
		.tag = tag,
		.tlen = tag ? strlen(tag) : 0,
		.opts = opts
	};

	char* name = VARLOG;
	struct tail tl, *ta = &tl;

	alloc_tail_buf(ta, 2*PAGE);
	open_logfile(ta, name);
	start_inotify(ta);

	seek_to_start_of_line(ta);
	process_chunk(ta, &gr);

	while(1) {
		flushout();

		int nf = wait_inotify(ta);

		slurp_tail(ta, &gr);

		if(!nf) continue;

		flush_buf(ta);
		reopen_logfile(ta);
		slurp_tail(ta, &gr);
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

	if(opts & OPT_f)
		follow_log(tag, opts);
	else
		dump_logs(tag, opts);

	flushout();

	return 0;
}
