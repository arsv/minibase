#include <sys/file.h>
#include <sys/mman.h>
#include <sys/inotify.h>

#include <errtag.h>
#include <util.h>

ERRTAG("tail");

#define OPTS "f"
#define OPT_f (1<<0)

struct top {
	int count;
	int opts;
	char* name;
	int fd;
	char* buf;
	int len;
};

static int start_inotify(char* name)
{
	int fd = xchk(sys_inotify_init(), "inotify-init", NULL);

	xchk(sys_inotify_add_watch(fd, name, IN_MODIFY), "inotify-add", name);

	return fd;
}

static void wait_inotify(int fd)
{
	char buf[500];
	int len = sizeof(buf);

	xchk(sys_read(fd, buf, len), "inotify", NULL);
}

void allocate_tail_buf(struct top* ctx, int len)
{
	void* brk = (void*)sys_brk(0);
	void* end = (void*)sys_brk(brk + len);

	if(end - brk < len)
		fail("cannot allocate memory", NULL, 0);

	ctx->buf = brk;
	ctx->len = end - brk;
}

static void output(char* buf, int len)
{
	sys_write(STDOUT, buf, len);
}

static void slurp_tail(struct top* ctx)
{
	int fd = ctx->fd;
	int rd;
	char* buf = ctx->buf;
	int len = ctx->len;

	while((rd = sys_read(fd, buf, len)) > 0)
		output(buf, rd);
}

static int count_newlines(struct top* ctx, int fd)
{
	int rd;
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

static void follow_tail(struct top* ctx)
{
	char* name = ctx->name;
	int in = start_inotify(name);

	while(1) {
		slurp_tail(ctx);
		wait_inotify(in);
	};
}

static int is_regular_file(struct stat* st)
{
	return ((st->mode & S_IFMT) == S_IFREG);
}

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
	
	allocate_tail_buf(ctx, 2*120*count);

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

static void seek_file_tail(struct top* ctx)
{
	int fd = ctx->fd;
	char* name = ctx->name;
	int count = ctx->count;

	int est = 120*count;
	struct stat st;

	xchk(sys_fstat(fd, &st), "stat", name);

	if(!is_regular_file(&st) || !st.size)
		return skip_file_tail(ctx);

	long maxbuf = (1<<20);
	long buflen = st.size > maxbuf ? maxbuf : st.size;
	allocate_tail_buf(ctx, buflen);

	if(est < st.size)
		xchk(sys_lseek(fd, -est, SEEK_END), "seek", name);

	int cnt = count_newlines(ctx, fd);
	int skip = cnt > count ? cnt - count : 0;

	if(est < st.size)
		xchk(sys_lseek(fd, -est, SEEK_END), "seek", name);
	else
		xchk(sys_lseek(fd, 0, SEEK_SET), "seek", name);

	read_skipping_first(ctx, skip);
}

static void run_file(int count, int opts, char* name)
{
	struct top context = {
		.count = count,
		.opts = opts,
		.name = name,
		.fd = xchk(sys_open(name, O_RDONLY), NULL, name)
	}, *ctx = &context;


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

	if(i < argc)
		run_file(count, opts, argv[i]);
	else
		run_pipe(count, opts);

	return 0;
}
