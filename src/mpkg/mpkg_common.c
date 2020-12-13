#include <sys/mman.h>
#include <sys/file.h>

#include <config.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "mpkg.h"

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

char* root_adjust(char* name)
{
	if(!name[0])
		fail("root-adjust empty name", NULL, 0);
	if(name[0] == '/')
		return name + 1;
	if(name[0] == '.' && name[1] == '/')
		return name + 2;

	return name;
}

void setup_root(CTX, int fromarg)
{
	char* name = fromarg ? shift(ctx) : HERE "/";
	int fd;

	if((fd = sys_open(name, O_DIRECTORY | O_PATH)) < 0)
		fail(NULL, name, fd);

	ctx->at = fd;
	ctx->root = name;
	ctx->rootfd = fd;
}

void setup_prefix(CTX, char* path)
{
	int fd, at = ctx->rootfd;
	int flags = O_DIRECTORY | O_PATH;

	path = root_adjust(path);

	if((fd = sys_openat(at, path, flags)) < 0)
		failz(ctx, NULL, path, fd);

	ctx->at = fd;
	ctx->pref = path;
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

static void set_repo_name(CTX, char* name)
{
	if(!name[0])
		fail("empty repo name", NULL, 0);
	if(check_repo_name(name))
		fail("invalid repo name", NULL, 0);

	ctx->repo = name;
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
	char* sep = strcbrk(name, ':');

	if(*sep) {
		*sep = '\0';
		set_repo_name(ctx, name);
		name = sep + 1;
	}

	set_package_name(ctx, name);
}

void take_pacfile_arg(CTX)
{
	char* pac = shift(ctx);

	ctx->pac = pac;
}

char* pac_name(CTX)
{
	char* str = ctx->pac;

	if(!str) fail("unset .pac name", NULL, 0);

	return str;
}

char* pkg_name(CTX)
{
	char* str = ctx->pkg;

	if(!str) fail("unset .pkg name", NULL, 0);

	return str;
}

char* reg_repo(CTX)
{
	return ctx->repo;
}

char* reg_name(CTX)
{
	char* str = ctx->name;

	if(!str) fail("unset package name", NULL, 0);

	return str;
}

/* Note distrinction: failz is used to report internal stuff like mpkg.conf
   or the .pkg files while failx/warnx gets called for the files being
   deployed or removed. Internal files are always root-relative, while
   package files may be relative to a prefix under the root. */

void failz(CTX, const char* msg, char* name, int err)
{
	char* root = ctx->root;
	int i, n = ctx->depth;
	int len = 16;

	if(root) len += strlen(root);

	for(i = 0; i < n; i++)
		len += strlen(ctx->path[i]) + 1;

	if(name) len += strlen(name);

	char* buf = alloca(len);
	char* p = buf;
	char* e = buf + len - 1;

	if(root) p = fmtstr(p, e, root);

	p = fmtstr(p, e, "/");

	for(i = 0; i < n; i++) {
		p = fmtstr(p, e, ctx->path[i]);
		p = fmtstr(p, e, "/");
	}

	if(name) p = fmtstr(p, e, name);

	*p++ = '\0';

	fail(msg, buf, err);
}

static void warn_pref_relative(CTX, const char* msg, char* name, int err)
{
	char* root = ctx->root;
	char* pref = ctx->pref;
	int i, n = ctx->depth;
	int len = 16;

	if(root) len += strlen(root) + 1;
	if(pref) len += strlen(pref) + 1;

	for(i = 0; i < n; i++)
		len += strlen(ctx->path[i]) + 1;

	if(name) len += strlen(name);

	char* buf = alloca(len);
	char* p = buf;
	char* e = buf + len - 1;

	if(root) {
		p = fmtstr(p, e, root);
		p = fmtstr(p, e, "/");
	} if(pref) {
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
