#include <sys/file.h>
#include <sys/mman.h>
#include <sys/inotify.h>

#include <string.h>
#include <memoff.h>
#include <util.h>
#include <main.h>

ERRTAG("tail");

#define OPTS "fh"
#define OPT_f (1<<0)
#define OPT_h (1<<1)

struct top {
	unsigned count;
	int opts;
	char* name;
	int fd;

	char* buf;
	size_t len;

	int inofd; /* inotify fd */
	int inowd; /* for the file itself */

	off_t size;
};

/* tail -f sets up inotify for the file itself and for the directory it
   resides in. Directory events are needed to catch log rotation: if inotify
   reports IN_CREATE with the same name as the file we're monitoring, we drop
   the old one and open the newly created file.

   This only makes sense for logs. */

static void dirname(char* path, char* buf, int len)
{
	char* p = buf;
	char* e = buf + len - 1;
	char *q, *sp = NULL;

	for(q = path; *q; q++) {
		if(p >= e) break;

		*p++ = *q;

		if(*q == '/') sp = p;
	}

	if(sp) {
		*sp = '\0';
	} else {
		/* assuming len >= 2 */
		buf[0] = '.';
		buf[1] = '\0';
	}
}

static void start_inotify(struct top* ctx)
{
	char* name = ctx->name;
	int fd, ret;

	if((fd = sys_inotify_init()) < 0)
		fail("inotify-init", NULL, fd);

	ctx->inofd = fd;

	if((ret = sys_inotify_add_watch(fd, name, IN_MODIFY)) < 0)
		fail("inotify-add", name, ret);

	ctx->inowd = ret;

	char dir[strlen(name)+2];
	dirname(name, dir, sizeof(dir));

	if((ret = sys_inotify_add_watch(fd, dir, IN_CREATE)) < 0)
		fail("inotify-add", dir, ret);
}

static int wait_inotify(struct top* ctx, char* base)
{
	int fd = ctx->inofd;
	char buf[500];
	int newfile = 0;
	int rd;

	if((rd = sys_read(fd, buf, sizeof(buf))) < 0)
		fail("inotify", NULL, rd);

	void* p = buf;
	void* e = buf + rd;

	while(p < e) {
		struct inotify_event* evt = p;

		if(evt->mask != IN_CREATE)
			;
		else if(!strcmp(evt->name, base))
			newfile = 1;

		p += sizeof(*evt) + evt->len;
	}

	return newfile;
}

static int open_file(char* name)
{
	int fd;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	return fd;
}

static void reopen_file(struct top* ctx)
{
	int fd = ctx->fd;
	char* name = ctx->name;

	warn("reopening", name, 0);

	sys_close(fd);

	ctx->fd = open_file(name);
	ctx->size = 0;

	int inofd = ctx->inofd;
	int inowd = ctx->inowd;
	int ret;

	if((ret = sys_inotify_rm_watch(inofd, inowd)) < 0)
		fail("inotify-rm", name, ret);

	if((ret = sys_inotify_add_watch(inofd, name, IN_MODIFY)) < 0)
		fail("inotify-add", name, ret);

	ctx->inowd = ret;
}

/* tail estimates the size (in bytes) the requested number of strings should
   take up at most, and then reads about that much data. If there are more
   than N strings there, the extra ones are skipped, otherwise the output
   will be less than N strings. Which isn't the worst options by the way,
   as it limits the about of garbage the users would get if the file happens
   to be non-text. */

unsigned estimate_size(unsigned count)
{
	return 120*count;
}

void allocate_tail_buf(struct top* ctx, int len)
{
	void* brk = sys_brk(0);
	void* end = sys_brk(brk + len);

	if(brk_error(brk, end))
		fail("cannot allocate memory", NULL, 0);

	ctx->buf = brk;
	ctx->len = end - brk;
}

static void output(char* buf, int len)
{
	int wr;

	if((wr = writeall(STDOUT, buf, len)) < 0)
		fail("write", "STDOUT", wr);
}

/* Fun fact: if we read() till the end of file, wait until it grows,
   and read() again, we'll get the appended data. No seeks needed. */

static void slurp_tail(struct top* ctx)
{
	int fd = ctx->fd;
	int rd;
	char* buf = ctx->buf;
	int len = ctx->len;

	while((rd = sys_read(fd, buf, len)) > 0)
		output(buf, rd);
}

/* Logs may get truncated during e.g. a daemon restart. This gets detected
   with a stat() that should(*) show the size decreasing. If this happens,
   tail rewinds the file and starts reading from the beginning. This only
   makes sense for logs.

   * actually it may not if the new file gets larger than the old one
   fast enough. This part is inherently racy. Again, logs only. */

static int check_truncation(struct top* ctx)
{
	int fd = ctx->fd;
	struct stat st;
	int ret;

	if((ret = sys_fstat(fd, &st)) < 0)
		return ret;

	if(st.size < ctx->size) {
		warn("rewinding", ctx->name, 0);

		if((ret = sys_seek(fd, 0)) < 0)
			fail("seek", ctx->name, ret);

		ctx->size = 0;

		return 1;
	}

	ctx->size = st.size;

	return 0;
}

static void follow_tail(struct top* ctx)
{
	char* name = ctx->name;
	char* base = basename(name);

	start_inotify(ctx);

	while(1) {
		slurp_tail(ctx);

		if(check_truncation(ctx))
			continue;
		if(!wait_inotify(ctx, base))
			continue;

		slurp_tail(ctx);
		reopen_file(ctx);
	};
}

/* To get the last N lines from a stream (or a non-seekable file), we read
   it into a ring buffer of estimated size until EOF and dump the last N
   lines from the buffer.

   The buffer layout is very simple, it's just buf[est] and we call read()
   to fill it completely over and over again. If the last pass filled it
   up to ptr (buf < ptr < end) and there was and the buffer has been filled
   completely at least once, then the est bytes at the end of the file are
   (ptr:end) + (buf:ptr). Processing is then done in two steps, (ptr:end)
   first and then (buf:ptr).

   First tail counts the lines in the buf (say M), then it skip the leading
   M - N lines noting the offset, and start dumping the buf contents from
   that offset. */

static void dump_from_offset(char* buf, char* ptr, char* end, int full, int off)
{
	int len = 0;

	if(full && off < (len = (end - ptr)))
		output(ptr + off, len - off);

	off -= len;

	if(off < (len = (ptr - buf)))
		output(buf + off, len - off);
}

static int count_lines_in(char* buf, int len)
{
	int cnt = 0;

	for(char* p = buf; p < buf + len; p++)
		if(*p == '\n')
			cnt++;

	return cnt;
}

static int count_lines(char* buf, char* ptr, char* end, int full)
{
	int cnt = 0;

	if(full && ptr < end)
		cnt += count_lines_in(ptr, end - ptr);
	if(ptr > buf)
		cnt += count_lines_in(buf, ptr - buf);

	return cnt;
}

long skip_nlines_in(char* buf, char* ptr, long off, int skip, int* lines)
{
	char* p;
	int cnt = *lines;

	for(p = buf; p < ptr; p++)
		if(*p == '\n' && ++cnt >= skip)
			return off + (p - buf) + 1;

	*lines = cnt;

	return 0;
}

int skip_nlines(char* buf, char* ptr, char* end, int full, int skip)
{
	int lines = 0;
	long off = 0;
	long ret;

	if(!skip)
		return 0;

	if(full && ptr < end) {
		if((ret = skip_nlines_in(ptr, end, off, skip, &lines)))
			return ret;
		off += end - ptr;
	}

	if(ptr > buf) {
		if((ret = skip_nlines_in(buf, ptr, off, skip, &lines)))
			return ret;
		off += ptr - buf;
	}

	return off + 1;
}

static void skip_file_tail(struct top* ctx)
{
	int count = ctx->count;

	allocate_tail_buf(ctx, estimate_size(count));

	char* buf = ctx->buf;
	char* end = buf + ctx->len;
	char* ptr = buf;
	int rd, fd = ctx->fd;
	int full = 0;

	while((rd = sys_read(fd, ptr, end - ptr)) > 0) {
		if(ptr < end) {
			ptr += rd;
		} else {
			ptr = buf;
			full = 1;
		}
	} if(rd < 0) fail("read", ctx->name, rd);

	int cnt = count_lines(buf, ptr, end, full);
	int skip = cnt > count ? cnt - count : 0;
	int off = skip_nlines(buf, ptr, end, full, skip);

	dump_from_offset(buf, ptr, end, full, off);
}

/* Here's how seekable files are treated: tail reads the high estimate
   of data at the end of the file and counts the lines there. If there
   are M > N lines there, it seeks back to the same position and repeats
   the read, but only starts writing them out after skipping the initial
   M - N lines.

   There are better ways to do it, but this approach is simple and if N
   is small it should work well. And N should always be small. */

static int count_tail_lines(struct top* ctx)
{
	int rd;
	int fd = ctx->fd;
	char* buf = ctx->buf;
	int len = ctx->len;
	char* p;
	int count = 0;

	while((rd = sys_read(fd, buf, len)) > 0)
		for(p = buf; p < buf + rd; p++)
			if(*p == '\n')
				count++;

	return count;
}

void read_skipping_first(struct top* ctx, int cnt)
{
	int fd = ctx->fd;
	char* buf = ctx->buf;
	int len = ctx->len;
	int rd;
	char* p;
	int seen = 0;

	while((rd = sys_read(fd, buf, len)) > 0) {
		for(p = buf; p < buf + rd; p++) {
			if(*p != '\n')
				continue;
			if(++seen > cnt) {
				output(p + 1, buf + rd - p - 1);
				break;
			}
		}
	} if(rd > 0) {
		slurp_tail(ctx);
	}
}

static int is_regular_file(struct stat* st)
{
	return ((st->mode & S_IFMT) == S_IFREG);
}

static void seek_file(struct top* ctx, off_t off)
{
	int ret;

	if((ret = sys_seek(ctx->fd, off)) < 0)
		fail("seek", ctx->name, ret);
}

static void seek_file_tail(struct top* ctx)
{
	unsigned count = ctx->count;
	unsigned est = estimate_size(count);
	struct stat st;
	int ret;

	if((ret = sys_fstat(ctx->fd, &st)) < 0)
		fail("stat", ctx->name, ret);

	if(!is_regular_file(&st) || !st.size)
		return skip_file_tail(ctx);

	ulong maxbuf = (1<<20);
	ulong buflen = maxbuf;

	if(mem_off_cmp(maxbuf, st.size) > 0)
		buflen = st.size;

	allocate_tail_buf(ctx, buflen);

	if(est < st.size)
		seek_file(ctx, st.size - est);

	unsigned cnt = count_tail_lines(ctx);
	unsigned skip = cnt > count ? cnt - count : 0;

	if(est < st.size)
		seek_file(ctx, st.size - est);
	else
		seek_file(ctx, 0);

	read_skipping_first(ctx, skip);
}

/* Head mode, because head is too small to be a standalone tool.
   Only shares the memory allocation with the tail code here. */

void skip_file_head(struct top* ctx)
{
	int fd = ctx->fd;
	char* buf = ctx->buf;
	int len = ctx->len;
	int count = ctx->count;
	int rd, seen = 0;

	while((rd = sys_read(fd, buf, len))) {
		char* p = buf;

		while(p < buf + rd)
			if(*p++ != '\n')
				continue;
			else if(++seen >= count)
				break;

		if(p > buf)
			output(buf, p - buf);
		if(p < buf + rd)
			break;

	} if(rd < 0) fail(NULL, ctx->name, rd);
}

/* Options parsing and context setup. */

static void run_file(int count, int opts, char* name)
{
	struct top context = {
		.count = count,
		.opts = opts,
		.name = name,
		.size = 0
	}, *ctx = &context;

	ctx->fd = open_file(name);

	seek_file_tail(ctx);

	if(!(opts & OPT_f)) return;

	follow_tail(ctx);
}

static void run_pipe(int count, int opts)
{
	struct top context = {
		.count = count,
		.opts = opts,
		.name = NULL,
		.fd = STDIN
	}, *ctx = &context;

	if(opts & OPT_f)
		fail("cannot follow stdin", NULL, 0);

	skip_file_tail(ctx);
}

static void run_head(int count, int opts, char* name)
{
	struct top context = {
		.count = count,
		.opts = opts,
		.name = name
	}, *ctx = &context;

	if(opts & OPT_f)
		fail("-f cannot be used with -h", NULL, 0);

	ctx->fd = name ? open_file(name) : STDIN;

	int est = estimate_size(count);

	allocate_tail_buf(ctx, est);

	skip_file_head(ctx);
}

static int isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

static void parse_opts(char* arg, int* opts, int* count)
{
	int cnt = 0;

	for(; isdigit(*arg); arg++)
		cnt = 10*cnt + (*arg - '0');
	if(cnt)
		*count = cnt;

	*opts = argbits(OPTS, arg);
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0, count = 10;

	if(i < argc && argv[i][0] == '-')
		parse_opts(argv[i++] + 1, &opts, &count);

	if(i < argc - 1)
		fail("too many arguments", NULL, 0);

	char* name = i < argc ? argv[i] : NULL;

	if(opts & OPT_h)
		run_head(count, opts, name);
	else if(name)
		run_file(count, opts, name);
	else
		run_pipe(count, opts);

	return 0;
}
