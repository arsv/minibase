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

static void open_list_file(CTX, char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	if(st.size > MAX_LIST_SIZE)
		fail(NULL, name, -E2BIG);

	int size = st.size;
	void* buf = heap_alloc(ctx, align4(size + 1));

	if((ret = sys_read(fd, buf, size)) < 0)
		fail("read", name, ret);
	if(ret != size)
		fail("incomplete read", NULL, 0);

	ctx->list.fd = fd;
	ctx->list.name = name;
	ctx->list.buf = buf;
	ctx->list.len = size;
}

static void syntax(CTX, char* msg)
{
	FMTBUF(p, e, buf, 200);
	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, ctx->list.name);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, ctx->list.line);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, msg);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);
	_exit(0xFF);
}

static int is_space(char c)
{
	return ((c == ' ') || (c == '\t'));
}

static char* skip_nonspace(char* p, char* e)
{
	for(; p < e; p++)
		if(is_space(*p))
			break;

	return p;
}

static char* skip_space(char* p, char* e)
{
	for(; p < e; p++)
		if(!is_space(*p))
			break;

	return p;
}

static char* back_space(char* p, char* e)
{
	while(p < e) {
		char* q = e - 1;

		if(!is_space(*q))
			break;

		e = q;
	}

	return e;
}

static void take_whole_line(CTX)
{
	char* ls = ctx->list.ls;
	uint len = ctx->list.le - ls;

	ctx->entry.path = ls;
	ctx->entry.plen = len;

	char* bn = basename(ls);

	ctx->entry.name = bn;
	ctx->entry.nlen = strlen(bn);
}

static void take_name_path(CTX)
{
	char* p = ctx->list.ls + 1;
	char* e = ctx->list.le;

	char* q = skip_nonspace(p, e);

	*q = '\0';

	ctx->entry.name = p;
	ctx->entry.nlen = strlen(p);

	p = skip_space(q + 1, e);

	ctx->entry.path = p;
	ctx->entry.plen = strlen(p);

	//if(nlen <= 0)
	//	syntax(lct, "empty link name");
	//if(tlen <= 0)
	//	syntax(lct, "empty link target");
	//if(nlen > PAGE)
	//	syntax(lct, "name too long");
	//if(tlen > PAGE)
	//	syntax(lct, "link too long");
}

static void common_put_file(CTX)
{
	int ret, at = ctx->at;
	char* path = ctx->entry.path;
	struct stat st;

	if((ret = sys_fstatat(at, path, &st, AT_SYMLINK_NOFOLLOW)) < 0)
		failz(ctx, path, ret);
	if(st.size > 0xFFFFFFFF)
		failz(ctx, path, -E2BIG);

	int type = st.mode & S_IFMT;

	ctx->entry.size = st.size;

	if(type == S_IFLNK)
		return put_symlink(ctx);
	if(type != S_IFREG)
		failz(ctx, path, -EINVAL);

	uint mode = (st.mode & 0111) ? 0755 : 0644;

	put_file(ctx, mode);
}

static void parse_file(CTX)
{
	take_whole_line(ctx);

	common_put_file(ctx);
}

static void parse_link(CTX)
{
	take_name_path(ctx);

	put_immlink(ctx);
}

static void parse_copy(CTX)
{
	take_name_path(ctx);

	common_put_file(ctx);
}

static void parse_path(CTX)
{
	char* p = ctx->list.ls + 1;
	char* e = ctx->list.le;

	p = skip_space(p, e);

	if(p >= e)
		syntax(ctx, "empty path");
	if(*p == '/')
		syntax(ctx, "leading slash");

	heap_reset(ctx, ctx->pref);

	int len = e - p;
	char* buf = heap_alloc(ctx, len + 2);

	memcpy(buf, p, len);
	buf[len++] = '/';
	buf[len] = '\0';

	ctx->pref = buf;
	ctx->plen = len;

	put_pref(ctx);
}

static char* grab_line(CTX)
{
	char* buf = ctx->list.buf;
	char* end = buf + ctx->list.len;

	char* s = ctx->list.lp;

	if(s >= end)
		return NULL;

	char* q = strecbrk(s, end, '\n');

	if(q >= end)
		syntax(ctx, "truncated file");
	if(s + PAGE <= q)
		syntax(ctx, "line too long");

	*q = '\0';

	s = skip_space(s, q);
	q = back_space(s, q);

	ctx->list.lp = q + 1;
	ctx->list.ls = s;
	ctx->list.le = q;

	ctx->list.line++;

	return s;
}

static void parse_input(CTX)
{
	char* ln;

	ctx->list.lp = ctx->list.buf;

	while((ln = grab_line(ctx))) {
		char lead = *ln;

		if(!lead) /* empty line */
			continue;
		else if(lead == '#')
			continue;
		else if(lead == '>')
			parse_path(ctx);
		else if(lead == '@')
			parse_link(ctx);
		else if(lead == '=')
			parse_copy(ctx);
		else
			parse_file(ctx);

		reset_entry(ctx);
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
	char* cpio = shift(ctx);
	char* list = shift(ctx);
	char* dir = NULL;

	if(got_more_arguments(ctx))
		dir = shift(ctx);

	no_more_arguments(ctx);

	ctx->at = AT_FDCWD;

	heap_init(ctx, 4*PAGE);
	open_list_file(ctx, list);
	make_cpio_file(ctx, cpio);

	if(dir) open_base_dir(ctx, dir);

	prep_initial_path(ctx);

	parse_input(ctx);

	put_trailer(ctx);
}
