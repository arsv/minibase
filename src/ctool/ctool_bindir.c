#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/mman.h>

#include <util.h>
#include <string.h>
#include <printf.h>
#include <format.h>
#include <config.h>

#include "ctool.h"

#define MAXCONF (1<<16)
#define MAXSCRIPT (1<<13)

#define MD_DRY 0
#define MD_REAL 1
#define MD_DELE 2

#define ST_NEUTRAL 0
#define ST_SCRIPT  1
#define ST_CONTENT 2

struct subcontext {
	struct top* top;

	char* path;
	char* buf;
	int size;
	int line;

	int binat;
	int mode;

	char* prefix;
	char* common;
	char* tooldir;

	int state;
	int outfd;
	int oldline;

	uint dataptr;

	void* ptr;
};

#define CCT struct subcontext* cct

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

int looks_like_path(char* name)
{
	char c, *p;

	for(p = name; (c = *p); p++)
		if(c == '/')
			return c;

	return 0;
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

static char* trim_right(char* ls, char* le)
{
	while(ls < le) {
		char* p = le - 1;

		if(!isspace(*p))
			break;

		le = p;
	}

	return le;
}

static void fail_syntax(CCT, const char* msg, char* arg)
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
	struct top* ctx = cct->top;

	int len = e - p;
	char* buf = alloc_align(ctx, len + 1);

	memcpy(buf, p, len);

	buf[len] = '\0';

	return buf;
}

static void append_raw(CCT, char* str, int len)
{
	struct top* ctx = cct->top;
	uint ptr = cct->dataptr;
	uint size = ctx->datasize;

	if(ptr + len >= size)
		fail_syntax(cct, "script too large", NULL);

	void* dst = ctx->databuf + ptr;

	memcpy(dst, str, len);

	cct->dataptr = ptr + len;
}

static void append_string(CCT, char* str)
{
	if(!str) return;

	append_raw(cct, str, strlen(str));
}

static char* vmatch(char* p, char* e, char* str)
{
	int left = e - p;
	int need = strlen(str);

	if(left < need)
		return NULL;
	if(memcmp(p, str, need))
		return NULL;

	return p + need;
}

static void append_content(CCT, char* p, char* e)
{
	while(p < e) {
		char* q = strecbrk(p, e, '%');

		if(q > p)
			append_raw(cct, p, q - p);

		if(q >= e) {
			break;
		} else if((p = vmatch(q, e, "%TOOL"))) {
			append_string(cct, cct->tooldir);
		} else if((p = vmatch(q, e, "%PREFIX"))) {
			append_string(cct, cct->prefix);
		} else {
			append_raw(cct, q, 1);
			p = q + 1;
		}
	}

	append_raw(cct, "\n", 1);
}

static void write_content(CCT)
{
	int ret, fd = cct->outfd;

	if(fd < 0)
		return;

	struct top* ctx = cct->top;
	void* buf = ctx->databuf;
	uint len = cct->dataptr;

	if((ret = writeall(fd, buf, len)) < 0)
		fail("write", NULL, ret);

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);

	cct->dataptr = 0;
	cct->outfd = -1;
}

static void flush_content(CCT)
{
	if(cct->state != ST_CONTENT)
		return;
	if(cct->mode == MD_REAL)
		write_content(cct);

	cct->state = ST_NEUTRAL;
}

static void do_dele(CCT, char* name)
{
	int ret, at = cct->binat;

	if((ret = sys_unlinkat(at, name, 0)) >= 0)
		return;
	if(ret == -ENOENT)
		return;
	if(ret == -ENOTDIR)
		return;

	fail(NULL, name, ret);
}

static void do_stat(CCT, char* name)
{
	int ret, at = cct->binat;
	struct stat st;

	if((ret = sys_fstatat(at, name, &st, 0)) >= 0)
		fail(NULL, name, -EEXIST);
	if(ret == -ENOENT)
		return;

	fail(NULL, name, ret);
}

static void do_link(CCT, char* link, char* target)
{
	int mode = cct->mode;

	if(mode == MD_DRY)
		return do_stat(cct, link);
	if(mode == MD_DELE)
		return do_dele(cct, link);

	int ret, at = cct->binat;

	if((ret = sys_symlinkat(target, at, link)) < 0)
		fail(NULL, link, ret);
}

static void do_open(CCT, char* name)
{
	int mode = cct->mode;

	if(mode == MD_DRY)
		return do_stat(cct, name);
	if(mode == MD_DELE)
		return do_dele(cct, name);

	int fd, at = cct->binat;
	int flags = O_CREAT | O_WRONLY;
	int cmode = 0755;

	if((fd = sys_openat4(at, name, flags, cmode)) < 0)
		fail(NULL, name, fd);

	cct->outfd = fd;
}

static void key_script(CCT, char* lp, char* le)
{
	char* sep = skip_nonspace(lp, le);
	char* next = skip_space(sep, le);

	if(next < le)
		fail_syntax(cct, "extra arguments", NULL);
	if(cct->state != ST_NEUTRAL)
		fail_syntax(cct, "misplaced script", NULL);

	char* name = copy_string(cct, lp, sep);

	if(looks_like_path(name))
		fail_syntax(cct, "invalid script name", NULL);

	do_open(cct, name);

	cct->state = ST_SCRIPT;
	cct->dataptr = 0;
}

static char* make_target(CCT, char* np, char* ne)
{
	char* prefix = cct->prefix;
	char* common = cct->common;

	char* name = np;
	int nlen = ne - np;
	int blen = nlen + 8;

	if(prefix)
		blen += strlen(prefix);
	if(common)
		blen += strlen(common);

	char* buf = alloc_align(cct->top, blen);

	char* p = buf;
	char* e = buf + blen - 1;

	if(prefix) {
		p = fmtstr(p, e, prefix);
		p = fmtstr(p, e, "/");
	} if(common) {
		p = fmtstr(p, e, common);
	}

	p = fmtraw(p, e, name, nlen);
	*p++ = '\0';

	return buf;
}

static void key_link(CCT, char* lp, char* le)
{
	char* sep = skip_nonspace(lp, le);
	char* next = skip_space(sep, le);

	char* link = copy_string(cct, lp, sep);
	char* target;

	if(looks_like_path(link))
		fail_syntax(cct, "invalid link name", NULL);

	if(next < le)
		target = make_target(cct, next, le);
	else if(!cct->prefix)
		fail_syntax(cct, "no prefix set", NULL);
	else
		target = make_target(cct, lp, sep);

	do_link(cct, link, target);

	heap_reset(cct->top, link);
}

static void key_prefix(CCT, char* lp, char* le)
{
	char* str = copy_string(cct, lp, le);
	char* sep = strcbrk(str, ':');

	cct->prefix = str;

	if(*sep) {
		*sep = '\0';
		cct->common = sep + 1;
	} else {
		cct->common = NULL;
	}
}

static const struct keyword {
	char word[16];
	void (*call)(CCT, char* lp, char* le);
} keywords[] = {
	{ "prefix",     key_prefix },
	{ "link",       key_link },
	{ "script",     key_script }
};

static char* skip_key(CCT, char* lp, char* le, char* key, int klen)
{
	char* kp = key;
	char* ke = key + klen - 1;

	while((lp < le) && (kp < ke)) {
		char c = *lp++;

		if(isspace(c))
			break;

		*kp++ = c;
	}

	if(kp >= ke)
		fail_syntax(cct, "key too long", NULL);
	if(kp == key)
		fail_syntax(cct, "empty key", NULL);

	*kp = '\0';

	while((lp < le) && isspace(*lp))
		lp++;

	return lp;
}

static void parse_keyword(CCT, char* ls, char* le)
{
	char key[16];
	char* lp = skip_key(cct, ls, le, key, sizeof(key));
	const struct keyword* kp;

	flush_content(cct);

	for(kp = keywords; kp < ARRAY_END(keywords); kp++)
		if(!strncmp(kp->word, key, sizeof(kp->word)))
			return kp->call(cct, lp, le);

	fail_syntax(cct, "unknown keyword", key);
}

static void parse_content(CCT, char* ls, char* le)
{
	if(cct->state == ST_NEUTRAL)
		fail_syntax(cct, "unexpected content", NULL);

	if(cct->state == ST_SCRIPT)
		cct->state = ST_CONTENT;

	if((ls < le) && (*ls == '>')) /* always true */
		ls++;
	if((ls < le) && (*ls == ' ')) /* should be true as well */
		ls++;

	append_content(cct, ls, le);
}

static void parse_line(CCT, char* ls, char* le)
{
	if(empty(ls, le))
		flush_content(cct);
	else if(*ls == '#')
		return;
	else if(*ls == '>')
		parse_content(cct, ls, le);
	else
		parse_keyword(cct, ls, le);
}

static void parse_tool_desc(CCT, int mode)
{
	char* p = cct->buf;
	char* e = p + cct->size;

	cct->mode = mode;

	while(p < e) {
		char* ls = p;
		char* le = strecbrk(p, e, '\n');

		p = le + 1;
		cct->line++;

		le = trim_right(ls, le);

		parse_line(cct, ls, le);
	}

	flush_content(cct);
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
	struct top* ctx = cct->top;
	int fd, ret;
	struct stat st;

	if((fd = sys_open(path, O_RDONLY)) < 0)
		fail(NULL, path, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", path, ret);
	if(st.size >= MAXCONF)
		fail(NULL, path, -E2BIG);

	uint size = st.size;
	uint dmax = MAXSCRIPT;
	uint extra = 2*PAGE;
	uint need = pagealign(size) + dmax + extra;
	void* buf = alloc_align(cct->top, need);

	void* dbuf = buf;
	void* rbuf = buf + dmax;

	if((ret = sys_read(fd, rbuf, size)) < 0)
		fail("read", NULL, ret);

	cct->path = path;
	cct->buf = rbuf;
	cct->size = size;
	cct->line = 0;

	ctx->databuf = dbuf;
	ctx->datasize = dmax;
}

static void setup_bin_dir(CCT)
{
	int fd;
	char* name = "bin";

	if((fd = sys_open(name, O_DIRECTORY)) < 0)
		fail(NULL, name, fd);

	cct->binat = fd;
}

static void prep_tooldir(CCT)
{
	char buf[1024];
	int ret;

	if((ret = sys_getcwd(buf, sizeof(buf))) < 0)
		fail("getcwd", NULL, ret);

	cct->tooldir = copy_string(cct, buf, buf + ret);
}

static void common_init(CTX, CCT)
{
	memzero(cct, sizeof(*cct));

	cct->binat = -1;
	cct->top = ctx;

	char* name = shift(ctx);

	no_more_arguments(ctx);

	check_workdir(ctx);
	setup_bin_dir(cct);

	char* path;

	if(looks_like_path(name))
		path = name;
	else
		path = prep_desc_name(cct, name);

	load_tool_desc(cct, path);
	prep_tooldir(cct);
}

void cmd_try(CTX)
{
	struct subcontext context, *cct = &context;

	common_init(ctx, cct);

	parse_tool_desc(cct, MD_DRY);
}

void cmd_use(CTX)
{
	struct subcontext context, *cct = &context;

	common_init(ctx, cct);

	parse_tool_desc(cct, MD_DRY);
	parse_tool_desc(cct, MD_REAL);
}

void cmd_unuse(CTX)
{
	struct subcontext context, *cct = &context;

	common_init(ctx, cct);

	parse_tool_desc(cct, MD_DELE);
}

void cmd_force(CTX)
{
	struct subcontext context, *cct = &context;

	common_init(ctx, cct);

	parse_tool_desc(cct, MD_DELE);
	parse_tool_desc(cct, MD_REAL);
}
