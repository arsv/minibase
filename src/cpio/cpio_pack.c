#include <sys/file.h>
#include <sys/mman.h>
#include <sys/fpath.h>
#include <sys/dents.h>
#include <sys/splice.h>

#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>

#include "cpio.h"

#define MAX_LIST_SIZE (1<<24)

struct listctx {
	struct top* ctx;

	int fd;
	char* name;

	void* buf;
	int len;

	int line;
};

#define LCT struct listctx* lct

static void open_list_file(LCT, char* name)
{
	int fd, ret;
	struct stat st;
	void* buf;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	if(st.size > MAX_LIST_SIZE)
		fail(NULL, name, -E2BIG);

	int size = st.size;
	int proto = PROT_READ;
	int flags = MAP_PRIVATE;

	buf = sys_mmap(NULL, size, proto, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", name, ret);

	lct->fd = fd;
	lct->name = name;
	lct->buf = buf;
	lct->len = size;
}

static void syntax(LCT, char* msg)
{
	FMTBUF(p, e, buf, 200);
	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, lct->name);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, lct->line);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, msg);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);
	_exit(0xFF);
}

static void pack_link(LCT, struct stat* st, char* path)
{
	struct top* ctx = lct->ctx;

	if(st->size > PAGE)
		failz(ctx, path, -E2BIG);

	char* name = basename(path);
	uint size = st->size;

	put_link(ctx, path, name, size);
}

static void pack_file(LCT, struct stat* st, char* path)
{
	struct top* ctx = lct->ctx;

	if(st->size > 0xFFFFFFFF)
		failz(ctx, path, -E2BIG);

	char* name = basename(path);
	uint size = st->size;
	int mode = (st->mode & 0111) ? 0755 : 0644;

	put_file(ctx, path, name, size, mode);
}

static void parse_file(LCT, char* p, char* e)
{
	struct top* ctx = lct->ctx;
	int nlen = e - p;
	char* real = alloca(nlen + 1);

	memcpy(real, p, nlen);
	real[nlen] = '\0';

	int ret, at = ctx->at;
	struct stat st;

	if((ret = sys_fstatat(at, real, &st, AT_SYMLINK_NOFOLLOW)) < 0)
		failz(ctx, real, ret);

	int type = st.mode & S_IFMT;

	if(type == S_IFLNK)
		return pack_link(lct, &st, real);
	if(type == S_IFREG)
		return pack_file(lct, &st, real);

	failz(ctx, real, -EINVAL);
}

static char* skip_nonspace(char* p, char* e)
{
	for(; p < e; p++)
		if(*p == ' ')
			break;

	return p;
}

static char* skip_space(char* p, char* e)
{
	for(; p < e; p++)
		if(*p != ' ')
			break;

	return p;
}

static void parse_link(LCT, char* p, char* e)
{
	struct top* ctx = lct->ctx;

	char* name = p;
	p = skip_nonspace(p, e);
	int nlen = p - name;
	p = skip_space(p, e);

	char* target = p;
	int tlen = e - p;

	if(nlen <= 0)
		syntax(lct, "empty link name");
	if(tlen <= 0)
		syntax(lct, "empty link target");
	if(nlen > PAGE)
		syntax(lct, "name too long");
	if(tlen > PAGE)
		syntax(lct, "link too long");

	put_immlink(ctx, name, nlen, target, tlen);
}

static void parse_path(LCT, char* p, char* e)
{
	struct top* ctx = lct->ctx;

	p = skip_space(p, e);

	if(p >= e)
		syntax(lct, "empty path");
	if(*p == '/')
		syntax(lct, "leading slash");

	heap_reset(ctx, ctx->pref);

	int len = e - p;
	char* buf = heap_alloc(ctx, len + 2);

	memcpy(buf, p, len);
	buf[len++] = '/';
	buf[len] = '\0';

	ctx->plen = len;

	put_pref(ctx);
}

static void parse_input(LCT)
{
	char* p = lct->buf;
	char* e = p + lct->len;

	while(p < e) {
		char* s = p;
		char* q = strecbrk(p, e, '\n');

		p = q + 1;

		lct->line++;

		if(s >= q)
			continue;
		if(s + PAGE < p)
			syntax(lct, "line too long");

		char lead = *s;

		if(lead == '#')
			continue;
		else if(lead == '>')
			parse_path(lct, s + 1, q);
		else if(lead == '@')
			parse_link(lct, s + 1, q);
		else
			parse_file(lct, s, q);
	}
}

static void prep_initial_path(CTX)
{
	char* path = heap_alloc(ctx, 1);
	path[0] = '\0';

	ctx->pref = path;
	ctx->plen = 0;
}

void cmd_pack(CTX)
{
	struct listctx context, *lct = &context;

	char* cpio = shift(ctx);
	char* list = shift(ctx);

	no_more_arguments(ctx);

	memzero(lct, sizeof(*lct));

	lct->ctx = ctx;
	ctx->at = AT_FDCWD;

	open_list_file(lct, list);
	make_cpio_file(ctx, cpio);

	heap_init(ctx, 4*PAGE);

	prep_initial_path(ctx);

	parse_input(lct);

	put_trailer(ctx);
}
