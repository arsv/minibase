#include <sys/mman.h>
#include <sys/file.h>

#include <config.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "mpkg.h"

#define DATADIR BASE_VAR "/mpkg"

static void* heap_alloc(CTX, int size)
{
	void* brk = ctx->brk;
	void* ptr = ctx->ptr;
	void* end = ctx->end;

	if(!brk) {
		brk = sys_brk(NULL);
		ptr = brk;
		end = brk;
		ctx->brk = ptr;
		ctx->end = end;
	}

	size = (size + 3) & ~3;

	void* new = ptr + size;

	if(new > end) {
		void* tmp = sys_brk(ptr + pagealign(size));

		if(brk_error(end, tmp))
			fail("brk", NULL, -ENOMEM);

		ctx->end = end = tmp;
	}

	ctx->ptr = new;

	return ptr;
}

void* alloc_tight(CTX, int size)
{
	return heap_alloc(ctx, size);
}

void* alloc_exact(CTX, int size)
{
	if(size % 4)
		fail("non-aligned exact alloc", NULL, 0);

	return heap_alloc(ctx, size);
}

void* alloc_align(CTX, int size)
{
	size = (size + 3) & ~3;

	return heap_alloc(ctx, size);
}

static int isalphanum(int c)
{
	if(c >= '0' && c <= '9')
		return c;
	if(c >= 'A' && c <= 'Z')
		return c;
	if(c >= 'a' && c <= 'z')
		return c;

	return 0;
}

static int check_pack_name(char* name)
{
	char* p = name;
	char c;

	if(!isalphanum(c = name[0]))
		return c;

	while((c = *p++))
		if(c < 0x20 || c >= 0x7F)
			return c;
		else if(c == '/')
			return c;

	return 0;
}

static int check_repo_name(char* name)
{
	char* p = name;
	char c;

	while((c = *p++))
		if(!isalphanum(c))
			return c;

	return 0;
}

static void set_repo_name(CTX, char* name, char* nend)
{
	if(name >= nend)
		fail("empty repo name", NULL, 0);

	int len = nend - name;
	char* str = alloc_align(ctx, len + 1);

	memcpy(str, name, len);
	str[len] = '\0';

	if(check_repo_name(str))
		fail("invalid repo name", str, 0);

	ctx->group = str;
}

static void set_package_name(CTX, char* name)
{
	if(!name[0])
		fail("empty package name", NULL, 0);
	if(check_pack_name(name))
		fail("invalid package name", NULL, 0);

	ctx->name = name;
}

void take_package_arg(CTX)
{
	char* name = shift(ctx);
	char* nend = strpend(name);

	if(!nend) goto name;

	char* sep = strecbrk(name, nend, ':');

	if(sep >= nend) goto name;

	set_repo_name(ctx, name, sep);
	name = sep + 1;
name:
	set_package_name(ctx, name);
}

void take_pacfile_arg(CTX)
{
	char* pac = shift(ctx);

	ctx->pacname = pac;
}

static void warn_pref_relative(CTX, const char* msg, char* name, int err)
{
	char* pref = ctx->prefix;
	int i, n = ctx->depth;
	int len = 16;

	if(pref) len += strlen(pref) + 1;

	for(i = 0; i < n; i++)
		len += strlen(ctx->path[i]) + 1;

	if(name) len += strlen(name);

	char* buf = alloca(len);
	char* p = buf;
	char* e = buf + len - 1;

	if(pref) {
		p = fmtstr(p, e, pref);
		p = fmtstr(p, e, "/");
	}

	for(i = 0; i < n; i++) {
		p = fmtstr(p, e, ctx->path[i]);
		p = fmtstr(p, e, "/");
	}

	if(name) p = fmtstr(p, e, name);

	*p++ = '\0';

	warn(msg, buf, err);
}

void warnx(CTX, const char* msg, char* name, int err)
{
	warn_pref_relative(ctx, msg, name, err);

	ctx->fail = 1;
}

void failx(CTX, const char* msg, char* name, int err)
{
	warn_pref_relative(ctx, msg, name, err);

	_exit(0xFF);
}

void need_zero_depth(CTX)
{
	if(ctx->depth)
		fail("non-zero inital depth", NULL, 0);
}

/* Given the package name, and the settings read from the config,
   assemble the full path to the .pac file to be installed.

   For instance opt:foo -> /var/repo/opt/foo.gz.pac */

void prep_pacname(CTX)
{
	char* name = ctx->name;
	char* repodir = ctx->repodir;
	char* suffix = ctx->suffix;

	if(!name)
		fail("empty package name", NULL, 0);
	if(!repodir)
		fail("empty repo dir", NULL, 0);

	int nlen = strlen(name);
	int rlen = strlen(repodir);
	int slen = suffix ? strlen(suffix) : 0;
	int len = nlen + rlen + slen + 10;

	char* path = alloc_align(ctx, len);
	char* p = path;
	char* e = path + len - 1;

	p = fmtstr(p, e, repodir);
	p = fmtchar(p, e, '/');
	p = fmtstr(p, e, name);
	p = fmtstr(p, e, ".pac");

	if(suffix) {
		p = fmtchar(p, e, '.');
		p = fmtstr(p, e, suffix);
	}

	*p++ = '\0';

	ctx->pacname = path;
}

void prep_lstname(CTX)
{
	char* group = ctx->group;
	char* name = ctx->name;

	int len = strlen(DATADIR) + 2;

	len += strlen(name) + 10;

	if(group) len += strlen(group) + 2;

	char* buf = alloc_align(ctx, len);

	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, DATADIR);
	p = fmtstr(p, e, "/");

	if(group) {
		p = fmtstr(p, e, group);
		p = fmtstr(p, e, "/");
	}

	p = fmtstr(p, e, name);
	p = fmtstr(p, e, ".list");

	*p++ = '\0';

	ctx->lstname = buf;
}

char* copy_string(CTX, char* p, char* e)
{
	int len = e - p;
	char* buf = alloc_align(ctx, len + 1);

	memcpy(buf, p, len);

	buf[len] = '\0';

	return buf;
}
