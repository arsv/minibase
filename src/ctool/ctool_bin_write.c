#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/mman.h>

#include <util.h>
#include <string.h>
#include <format.h>
#include <config.h>

#include "ctool.h"
#include "ctool_bin.h"

/* The code here enacts individual keywords from the toolchain description.

   Note the heap gets reset between between each call, so these functions
   call allocate whichever temp values they need but the results are not
   persistent and cannot be stored. */

static void bad_mode_trap(CCT)
{
	fail_syntax(cct, "requested mode missing", NULL);
}

static void unlink_file(char* name)
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

static void check_isdir(char* name)
{
	int ret;
	struct stat st;

	if((ret = sys_stat(name, &st)) < 0)
		fail(NULL, name, ret);
	if((st.mode & S_IFMT) != S_IFDIR)
		fail(NULL, name, -ENOTDIR);
}

static void check_nexist(char* name)
{
	int ret;
	struct stat st;

	if((ret = sys_stat(name, &st)) >= 0)
		fail(NULL, name, -EEXIST);
	if(ret == -ENOENT)
		return;

	fail(NULL, name, ret);
}

static void link_tool(char* link, char* target)
{
	int ret;

	if((ret = sys_symlink(target, link)) < 0)
		fail(NULL, link, ret);
}

static void link_repo(char* path)
{
	char* name = "rep";
	struct stat st;
	int ret;

	if((ret = sys_stat(path, &st)) < 0)
		fail(NULL, path, ret);
	if((st.mode & S_IFMT) != S_IFDIR)
		fail(NULL, path, -ENOTDIR);

	if((ret = sys_unlink(name)) >= 0)
		;
	else if(ret == -ENOENT)
		;
	else
		fail(NULL, name, ret);

	if((ret = sys_symlink(path, name)) < 0)
		fail(NULL, name, ret);
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
	read_template(cct, src);
	substitute_vars(cct);
	write_results(cct, dst, md);
}

static char* make_dst_path(CCT, char* dir, char* name)
{
	char* dend = strpend(dir);
	char* nend = strpend(name);
	int need = strelen(dir, dend) + strelen(name, nend) + 4;

	char* path = alloc_align(cct->top, need);

	char* p = path;
	char* e = p + need - 1;

	if(dend) {
		p = fmtstre(p, e, dir, dend);
		p = fmtchar(p, e, '/');
	}

	p = fmtstre(p, e, name, nend);

	*p++ = '\0';

	return path;
}

static char* make_src_path(CCT, char* dir, char* name)
{
	char* pref = BASE_ETC "/tool";
	char* pend = strpend(pref);

	char* dend = strpend(dir);
	char* nend = strpend(name);
	int need = strelen(pref, pend) + strelen(dir, dend) + strelen(name, nend) + 4;

	char* path = alloc_align(cct->top, need);
	char* p = path;
	char* e = p + need - 1;

	p = fmtstre(p, e, pref, pend);
	p = fmtchar(p, e, '/');
	p = fmtstre(p, e, dir, dend);
	p = fmtchar(p, e, '/');
	p = fmtstre(p, e, name, nend);

	*p++ = '\0';

	return path;
}

static char* make_link_target(CCT, char* name)
{
	char* prefix = cct->prefix;
	char* common = cct->common;

	char* nend = strpend(name);
	int nlen = strelen(name, nend);
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

static void add_path(CCT, char* path)
{
	void* buf = cct->wrbuf;
	uint size = cct->wrsize;
	uint ptr = cct->wrptr;
	uint left = size - ptr;

	uint len = strnlen(path, left);

	if(len + 1 >= left)
		fail("file list overflow", NULL, 0);

	char* dst = buf + ptr;

	memcpy(dst, path, len);
	dst[len] = '\n';

	cct->wrptr = ptr + len + 1;
}

void do_link(CCT, char* dst, char* target)
{
	int mode = cct->mode;

	char* dstpath = make_dst_path(cct, "bin", dst);
	char* tgtpath = make_link_target(cct, target);

	if(mode == MD_DRY)
		check_nexist(dstpath);
	else if(mode == MD_DELE)
		unlink_file(dstpath);
	else if(mode == MD_REAL)
		link_tool(dstpath, tgtpath);
	else if(mode == MD_LIST)
		add_path(cct, dstpath);
	else
		bad_mode_trap(cct);
}

void do_config(CCT, char* dst, char* src)
{
	int mode = cct->mode;

	char* dstpath = make_dst_path(cct, NULL, dst);
	char* srcpath = make_src_path(cct, "config", src);

	if(mode == MD_DRY)
		check_nexist(dstpath);
	else if(mode == MD_DELE)
		unlink_file(dstpath);
	else if(mode == MD_REAL)
		copy_with_subst(cct, dstpath, srcpath, 0644);
	else if(mode == MD_LIST)
		add_path(cct, dstpath);
	else
		bad_mode_trap(cct);

}

void do_script(CCT, char* dst, char* src, char* tool)
{
	int mode = cct->mode;

	char* dstpath = make_dst_path(cct, "bin", dst);
	char* srcpath = make_src_path(cct, "script", src);

	cct->tool = make_link_target(cct, tool);

	if(mode == MD_DRY)
		check_nexist(dstpath);
	else if(mode == MD_DELE)
		unlink_file(dstpath);
	else if(mode == MD_REAL)
		copy_with_subst(cct, dstpath, srcpath, 0755);
	else if(mode == MD_LIST)
		add_path(cct, dstpath);
	else
		bad_mode_trap(cct);

	cct->tool = NULL;
}

void do_repo(CCT, char* path)
{
	int mode = cct->mode;

	if(mode == MD_DRY)
		check_isdir(path);
	else if(mode == MD_DELE)
		unlink_file("rep");
	else if(mode == MD_REAL)
		link_repo(path);
	else if(mode == MD_LIST)
		;
	else
		bad_mode_trap(cct);
}
