#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/mman.h>

#include <util.h>
#include <string.h>
#include <format.h>
#include <config.h>

#include "ctool.h"
#include "ctool_bin.h"

/* The code here takes the name of the tool description file
   and parses into a sequence of struct stmt-s. This is mostly
   done to have all the strings at hand, and allows for instance
   keeping a pointer to whatever followed `prefix` 5 lines ago.

   Very little processing actually happens at this stage. */

struct stmt {
	short len;
	short line;
	short code;
	short arg[3];
	char payload[];
};

static int isspace(char c)
{
	return (c == ' ' || c == '\t');
}

static int empty(char* p, char* e)
{
	while(p < e && isspace(*p))
		p++;

	return (p >= e);
}

static int looks_like_pathx(char* p, char* e)
{
	char c;

	if(!p || !e)
		fail(NULL, NULL, -EFAULT);

	for(; p < e; p++)
		if((c = *p) == '/')
			return c;

	return 0;
}

int looks_like_path(char* name)
{
	return looks_like_pathx(name, strpend(name));
}

static char* skip_word(char* p, char* e)
{
	for(; p < e; p++)
		if(isspace(*p))
			break;

	return p;
}

static char* skip_nonspace(char* p, char* e)
{
	for(; p < e; p++)
		if(isspace(*p))
			break;

	return p;
}

static char* skip_space(char* p, char* e)
{
	for(; p < e; p++)
		if(!isspace(*p))
			break;

	return p;
}

static char* st_arg_str(ST, int i)
{
	if(i < 0 || i >= (int)ARRAY_SIZE(st->arg))
		return NULL;

	int off = st->arg[i];
	void* ref = (void*)st;

	if(off <= 0 || off > PAGE)
		return NULL;

	return (char*)(ref + off);
}

void fail_syntax(CCT, const char* msg, char* arg)
{
	FMTBUF(p, e, buf, 200);

	p = fmtstr(p, e, cct->path);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, cct->line);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, msg);

	if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	}

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	_exit(0xFF);
}

static char* copy_string(CCT, char* p, char* e)
{
	int len = e - p;
	char* buf = alloc_align(cct->top, len + 1);

	memcpy(buf, p, len);

	buf[len] = '\0';

	return buf;
}

static void prepare_buffers(CCT)
{
	if(cct->rdbuf)
		return;

	int max = (1<<16); /* 64KB */
	int need = max + max;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	void* buf = sys_mmap(NULL, need, prot, flags, -1, 0);
	int ret;

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	cct->rdbuf = buf;
	cct->rdsize = max;
	cct->rdlen = 0;

	cct->wrbuf = buf + max;
	cct->wrsize = max;
	cct->wrptr = 0;
}

static void run_link(CCT, ST)
{
	char* dst = st_arg_str(st, 0);
	char* tgt = st_arg_str(st, 1);

	do_link(cct, dst, tgt);
}

static void run_script(CCT, ST)
{
	char* dst = st_arg_str(st, 0);
	char* src = st_arg_str(st, 1);
	char* tool = st_arg_str(st, 2);

	do_script(cct, dst, src, tool);
}

static void run_repo(CCT, ST)
{
	char* path = st_arg_str(st, 0);

	do_repo(cct, path);
}

static void run_config(CCT, ST)
{
	char* dst = st_arg_str(st, 0);
	char* src = st_arg_str(st, 1);

	cct->config = dst;

	do_config(cct, dst, src);
}

static void run_prefix(CCT, ST)
{
	cct->prefix = st_arg_str(st, 0);
	cct->common = st_arg_str(st, 1);
}

static void run_interp(CCT, ST)
{
	cct->interp = st_arg_str(st, 0);
}

static void set_st_arg(ST, int i, char* arg)
{
	void* ref = (void*)st;
	void* ptr = (void*)arg;
	long off = ptr - ref;

	if(off < 0 || off >= PAGE)
		fail("st offset fault", NULL, 0);

	st->arg[i] = off;
}

static void copy_arg(CCT, ST, int i, char* arg)
{
	set_st_arg(st, i, copy_string(cct, arg, strpend(arg)));
}

static void copy_args(CCT, ST, int min, int max)
{
	int argn = cct->argn;
	char** args = cct->args;

	if(argn < min)
		fail_syntax(cct, "argument required", NULL);
	if(argn > max)
		fail_syntax(cct, "extra arguments", NULL);

	for(int i = 0; i < argn; i++)
		copy_arg(cct, st, i, args[i]);
}

static void clone_arg(CCT, ST, int dst, int src)
{
	if(!st->arg[src])
		fail_syntax(cct, "clone arg fault", NULL);
	if(st->arg[dst])
		return;

	st->arg[dst] = st->arg[src];
}

static void key_config(CCT, ST)
{
	copy_args(cct, st, 1, 2);
	/* config foo = config foo bar */
	clone_arg(cct, st, 1, 0);
}

static void key_script(CCT, ST)
{
	copy_args(cct, st, 1, 3);
	/* script foo = script foo bar */
	clone_arg(cct, st, 1, 0);
	/* script foo bar = script foo bar foo */
	clone_arg(cct, st, 2, 0);
}

static void key_link(CCT, ST)
{
	copy_args(cct, st, 1, 2);
	/* link foo = link foo bar */
	clone_arg(cct, st, 1, 0);
}

static void key_prefix(CCT, ST)
{
	copy_args(cct, st, 1, 1);

	/* prefix foo:bar -> prefix foo bar */

	char* arg = st_arg_str(st, 0);

	if(!arg) return;

	char* end = strpend(arg);
	char* sep = strecbrk(arg, end, ':');

	if(sep >= end) return;

	*sep = '\0';

	set_st_arg(st, 1, sep + 1);
}

static void key_repo(CCT, ST)
{
	copy_args(cct, st, 1, 1);
}

static void key_interp(CCT, ST)
{
	copy_args(cct, st, 1, 1);
}

static const struct keyword {
	char word[16];
	void (*key)(CCT, ST);
	void (*run)(CCT, ST);
} keywords[] = {
	{ "prefix",     key_prefix,  run_prefix },
	{ "config",     key_config,  run_config },
	{ "interp",     key_interp,  run_interp },
	{ "link",       key_link,    run_link   },
	{ "repo",       key_repo,    run_repo   },
	{ "script",     key_script,  run_script }
};

void run_statements(CCT, int mode)
{
	void* ptr = cct->stmts;
	void* end = cct->stend;

	cct->mode = mode;

	cct->wrptr = 0;
	cct->rdlen = 0;

	while(ptr < end) {
		struct stmt* st = ptr;

		ptr += st->len;

		int code = st->code;

		if((code < 0) || (code >= (int)ARRAY_SIZE(keywords)))
			fail("statements code fault", NULL, 0);

		const struct keyword* kw = &keywords[code];

		if(!kw->run)
			continue;

		cct->line = st->line;

		cct->heap = cct->top->ptr;
		kw->run(cct, st);
		cct->top->ptr = cct->heap;
	}
}

static void parse_key(CCT, ST, char* p, char* e)
{
	uint len = (int)(e - p);
	int i, n = ARRAY_SIZE(keywords);

	for(i = 0; i < n; i++) {
		const struct keyword* kp = &keywords[i];

		if(strnlen(kp->word, sizeof(kp->word)) != len)
			continue;
		if(memcmp(kp->word, p, len))
			continue;

		st->code = i;

		return kp->key(cct, st);
	}

	fail_syntax(cct, "unknown keyword", NULL);
}

static void set_stmt_size(CCT, ST)
{
	void* ptr = cct->top->ptr;
	void* sta = (void*)st;

	int len = ptr - sta;

	if(len < 0 || len > PAGE)
		fail_syntax(cct, "stmt memory fault", NULL);

	st->len = len;
}

static void parse_arg(CCT, char* p, char* e)
{
	int len = e - p;
	int need = len + 1;

	uint off = cct->wrptr;
	uint new = off + need;

	if(cct->argn >= ARRAY_SIZE(cct->args))
		fail_syntax(cct, "too many args", NULL);
	if(new > cct->rdsize)
		fail_syntax(cct, "line too long", NULL);

	char* dst = cct->wrbuf + off;

	memcpy(dst, p, len);
	dst[len] = '\0';

	cct->args[cct->argn++] = dst;
	cct->wrptr = new;
}

static void parse_line(CCT, char* p, char* e)
{
	char* q;

	if(*p == '#')
		return;
	if(empty(p, e))
		return;

	q = skip_nonspace(p, e);
	char* kp = p;
	char* ke = q;
	p = skip_space(q, e);

	struct stmt* st = alloc_exact(cct->top, sizeof(*st));

	st->line = cct->line;

	cct->argn = 0;
	cct->wrptr = 0;

	memzero(cct->args, sizeof(cct->args));

	while(p < e) {
		q = skip_word(p, e);
		parse_arg(cct, p, q);
		p = skip_space(q, e);
	} if(p < e) {
		fail_syntax(cct, "too many arguments", NULL);
	}

	parse_key(cct, st, kp, ke);

	set_stmt_size(cct, st);
}

static void parse_tool_desc(CCT)
{
	char* p = cct->rdbuf;
	char* e = p + cct->rdlen;

	cct->stmts = cct->top->ptr;

	while(p < e) {
		char* q = strecbrk(p, e, '\n');

		cct->line++;

		parse_line(cct, p, q);

		p = q + 1;
	}

	cct->stend = cct->top->ptr;

	cct->argn = 0;
	memzero(cct->args, sizeof(cct->args));
}

static char* prep_desc_name(CCT, char* name)
{
	char* pref = BASE_ETC "/tool/";
	int nlen = strlen(name);
	int plen = strlen(pref);
	int need = nlen + plen + 10;

	char* buf = alloc_align(cct->top, need);
	char* p = buf;
	char* e = buf + need - 1;

	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, name);
	p = fmtstr(p, e, ".desc");
	*p++ = '\0';

	return buf;
}

static void load_tool_desc(CCT, char* path)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(path, O_RDONLY)) < 0)
		fail(NULL, path, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", path, ret);
	if(st.size >= cct->rdsize)
		fail(NULL, path, -E2BIG);

	uint size = st.size;
	void* buf = cct->rdbuf;

	if((ret = sys_read(fd, buf, size)) < 0)
		fail("read", NULL, ret);

	cct->path = path;
	cct->rdlen = ret;
}

static void prep_tooldir(CCT)
{
	char buf[1024];
	int ret;

	if((ret = sys_getcwd(buf, sizeof(buf))) < 0)
		fail("getcwd", NULL, ret);

	cct->tooldir = copy_string(cct, buf, buf + ret);
}

void common_bin_init(CTX, CCT)
{
	memzero(cct, sizeof(*cct));

	cct->top = ctx;
	cct->fd = -1;

	char* name = shift(ctx);

	no_more_arguments(ctx);

	char* path;

	if(looks_like_path(name))
		path = name;
	else
		path = prep_desc_name(cct, name);

	prepare_buffers(cct);

	load_tool_desc(cct, path);
	prep_tooldir(cct);

	parse_tool_desc(cct);
}
