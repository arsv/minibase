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

struct subcontext {
	struct top* top;

	char* path;
	char* buf;
	int size;
	int line;

	int mode;

	char* prefix;
	char* common;

	char* config;
	char* interp;
	char* tooldir;
	char* tool;

	int outfd;
	int oldline;

	void* descbuf;
	uint descsize;

	void* rdbuf;
	uint rdsize;
	uint rdlen;

	void* wrbuf;
	uint wrsize;
	uint wrptr;

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
	int len = e - p;
	char* buf = alloc_align(cct->top, len + 1);

	memcpy(buf, p, len);

	buf[len] = '\0';

	return buf;
}

static void do_unlink(char* name)
{
	int ret;

	if((ret = sys_unlink(name)) >= 0)
		return;
	if(ret == -ENOENT)
		return;
	if(ret == -ENOTDIR)
		return;

	fail(NULL, name, ret);
}

static void do_stat(char* name)
{
	int ret;
	struct stat st;

	if((ret = sys_stat(name, &st)) >= 0)
		fail(NULL, name, -EEXIST);
	if(ret == -ENOENT)
		return;

	fail(NULL, name, ret);
}

static void link_tool(CCT, char* link, char* target)
{
	int mode = cct->mode;

	if(mode == MD_DRY)
		return do_stat(link);
	if(mode == MD_DELE)
		return do_unlink(link);

	int ret;

	if((ret = sys_symlink(target, link)) < 0)
		fail(NULL, link, ret);
}

static void append_raw(CCT, char* p, char* e)
{
	void* buf = cct->wrbuf;
	int size = cct->wrsize;
	int ptr = cct->wrptr;

	if(!p || !e) return;

	int left = size - ptr;
	int need = e - p;

	if(need > left)
		fail_syntax(cct, "output buf overflow", NULL);

	memcpy(buf + ptr, p, need);

	cct->wrptr = ptr + need;
}

static void append_str(CCT, char* s)
{
	return append_raw(cct, s, strpend(s));
}

static void var_tooldir(CCT)
{
	return append_str(cct, cct->tooldir);
}

static void var_config(CCT)
{
	return append_str(cct, cct->config);
}

static void var_interp(CCT)
{
	return append_str(cct, cct->interp);
}

static void var_tool(CCT)
{
	return append_str(cct, cct->tool);
}

static const struct variable {
	char name[8];
	void (*call)(CCT);
} variables[] = {
	{ "TOOL",    var_tool    },
	{ "TOOLDIR", var_tooldir },
	{ "INTERP",  var_interp  },
	{ "CONFIG",  var_config  }
};

static void append_var(CCT, char* p, char* e)
{
	const struct variable* vr;

	if(e <= p + 1) /* non-uppercase variable */
		goto skip;
	if(e > p + 16) /* too long */
		goto skip;

	char* var = p + 1;
	int len = e - p - 1;

	for(vr = variables; vr < ARRAY_END(variables); vr++) {
		int need = strnlen(vr->name, sizeof(vr->name));

		if(need != len)
			continue;
		if(memcmp(var, vr->name, len))
			continue;

		return vr->call(cct);
	}

skip:
	append_raw(cct, p, e);
}

static char* skip_varname(char* p, char* e)
{
	if(p >= e || *p != '$')
		return p;

	for(p++; p < e; p++) {
		char c = *p;

		if(c < 'A' || c > 'Z')
			break;
	}

	return p;
}

static void substitute_vars(CCT)
{
	void* buf = cct->rdbuf;
	int len = cct->rdlen;

	char* p = buf;
	char* e = p + len;

	cct->wrptr = 0;

	while(p < e) {
		char* q = strecbrk(p, e, '$');

		if(q > p)
			append_raw(cct, p, q);

		char* r = skip_varname(q, e);

		if(r > q)
			append_var(cct, q, r);
		else
			break;

		p = r;
	}
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

static void read_template(CCT, char* name)
{
	int fd, ret;
	int size = cct->rdsize;
	void* buf = cct->rdbuf;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);
	if((st.mode & S_IFMT) != S_IFREG)
		fail("not a regular file:", name, 0);
	if(st.size > size)
		fail(NULL, name, -E2BIG);

	if((ret = sys_read(fd, buf, size)) < 0)
		fail("read", name, ret);

	cct->rdlen = ret;

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);
}

static void write_results(CCT, char* name, int mode)
{
	int fd, ret;
	int flags = O_WRONLY | O_TRUNC | O_CREAT;

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail(NULL, name, fd);

	void* buf = cct->wrbuf;
	int size = cct->wrptr;

	if((ret = sys_write(fd, buf, size)) < 0)
		fail(NULL, name, ret);
	else if(ret != size)
		fail(NULL, name, -EINTR);

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, fd);
}

static void copy_with_subst(CCT, char* dst, char* src, int md)
{
	int mode = cct->mode;

	if(mode == MD_DRY)
		return do_stat(dst);
	if(mode == MD_DELE)
		return do_unlink(dst);

	prepare_buffers(cct);
	read_template(cct, src);
	substitute_vars(cct);
	write_results(cct, dst, md);
}

static char* make_dst_path(CCT, char* dir, char* sp, char* se)
{
	char* dend = strpend(dir);
	int need = strelen(dir, dend) + strelen(sp, se) + 4;

	char* path = alloc_align(cct->top, need);
	char* p = path;
	char* e = p + need - 1;

	if(dend) {
		p = fmtstre(p, e, dir, dend);
		p = fmtchar(p, e, '/');
	}

	p = fmtstre(p, e, sp, se);

	*p++ = '\0';

	return path;
}

static char* make_src_path(CCT, char* dir, char* sp, char* se)
{
	char* pref = BASE_ETC "/tool";
	char* pend = strpend(pref);

	char* dend = strpend(dir);
	int need = strelen(pref, pend) + strelen(dir, dend) + strelen(sp, se) + 4;

	char* path = alloc_align(cct->top, need);
	char* p = path;
	char* e = p + need - 1;

	p = fmtstre(p, e, pref, pend);
	p = fmtchar(p, e, '/');
	p = fmtstre(p, e, dir, dend);
	p = fmtchar(p, e, '/');
	p = fmtstre(p, e, sp, se);

	*p++ = '\0';

	return path;
}

static char* make_target(CCT, char* np, char* ne)
{
	char* prefix = cct->prefix;
	char* common = cct->common;

	char* name = np;
	int nlen = strelen(np, ne);
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
		p = fmtstr(p, e, "/");
	}

	p = fmtraw(p, e, name, nlen);
	*p++ = '\0';

	return buf;
}

static void key_config(CCT, char* lp, char* le)
{
	void* ptr = cct->top->ptr;

	char* dst = lp;
	char* dse = skip_nonspace(dst, le);

	char* src = skip_space(dse, le);
	char* sre = skip_nonspace(src, le);

	char* tail = skip_space(sre, le);

	if(tail < le)
		fail_syntax(cct, "extra arguments", NULL);

	if(src >= le)
		src = dst;

	char* dstpath = make_dst_path(cct, NULL, dst, dse);
	char* srcpath = make_src_path(cct, "config", src, sre);

	copy_with_subst(cct, dstpath, srcpath, 0644);

	heap_reset(cct->top, ptr);

	cct->config = copy_string(cct, dst, dse);
}

static void key_script(CCT, char* lp, char* le)
{
	void* ptr = cct->top->ptr;

	char* dst = lp;
	char* dse = skip_nonspace(dst, le);

	char* src = skip_space(dse, le);
	char* sre = skip_nonspace(src, le);

	char* tlp = skip_space(sre, le);
	char* tle = skip_nonspace(tlp, le);

	char* tail = skip_space(tle, le);

	if(tail < le)
		fail_syntax(cct, "extra arguments", NULL);

	if(src >= le)
		src = dst;

	if(tlp < le)
		cct->tool = make_target(cct, tlp, tle);
	else
		cct->tool = make_target(cct, dst, dse);

	char* dstpath = make_dst_path(cct, "bin", dst, dse);
	char* srcpath = make_src_path(cct, "script", src, sre);

	copy_with_subst(cct, dstpath, srcpath, 0755);

	cct->tool = NULL;

	heap_reset(cct->top, ptr);
}

static void key_link(CCT, char* lp, char* le)
{
	void* ptr = cct->top->ptr;

	char* dst = lp;
	char* dse = skip_nonspace(dst, le);

	char* tgt = skip_space(dse, le);
	char* tge = skip_nonspace(tgt, le);

	char* link = make_dst_path(cct, "bin", dst, dse);

	if(tgt >= le)
		tgt = dst;

	char* target = make_target(cct, tgt, tge);

	link_tool(cct, link, target);

	heap_reset(cct->top, ptr);
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

static void key_interp(CCT, char* lp, char* le)
{
	cct->interp = copy_string(cct, lp, le);
}

static const struct keyword {
	char word[16];
	void (*call)(CCT, char* lp, char* le);
} keywords[] = {
	{ "prefix",     key_prefix },
	{ "config",     key_config },
	{ "interp",     key_interp },
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

static void parse_line(CCT, char* ls, char* le)
{
	char key[16];

	if(*ls == '#')
		return;
	if(empty(ls, le))
		return;

	char* lp = skip_key(cct, ls, le, key, sizeof(key));
	const struct keyword* kp;

	for(kp = keywords; kp < ARRAY_END(keywords); kp++)
		if(!strncmp(kp->word, key, sizeof(kp->word)))
			return kp->call(cct, lp, le);

	fail_syntax(cct, "unknown keyword", key);
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

	cct->descbuf = dbuf;
	cct->descsize = dmax;
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

	cct->top = ctx;

	char* name = shift(ctx);

	no_more_arguments(ctx);

	check_workdir(ctx);

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

	//parse_tool_desc(cct, MD_DRY);
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
