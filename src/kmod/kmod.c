#include <sys/open.h>
#include <sys/fstat.h>
#include <sys/mmap.h>
#include <sys/uname.h>
#include <sys/initmodule.h>
#include <sys/deletemodule.h>
#include <sys/_exit.h>

#include <alloca.h>
#include <string.h>
#include <format.h>
#include <util.h>
#include <fail.h>

ERRTAG = "kmod";
ERRLIST = {
	REPORT(EACCES), REPORT(EAGAIN), REPORT(EBADF), REPORT(EINVAL),
	REPORT(ENFILE), REPORT(ENODEV), REPORT(ENOMEM), REPORT(EPERM),
	REPORT(ETXTBSY), REPORT(EOVERFLOW), REPORT(EBADMSG), REPORT(EBUSY),
	REPORT(EFAULT), REPORT(ENOKEY), REPORT(EEXIST), REPORT(ENOEXEC),
	RESTASNUMBERS
};

#define MAX_RELEASE_LEN 65
#define MAX_FILE_SIZE 20*1024*1024 /* 20MB */

#define OPTS "r"
#define OPT_r (1<<0)	/* rmmod */

struct modctx {
	char* name;	/* "e1000e" */
	char* dir;	/* "/lib/modules/4.8.13-1-ARCH/" */

	char* deps;	/* mmaped modules.dep and its length */
	long dlen;	

	char* ls;	/* line for this particular module */
	char* le;
	char* sep;
};

static int looks_like_path(char* name)
{
	char* p;

	for(p = name; *p; p++)
		if(*p == '/' || *p == '.')
			return 1;

	return 0;
}

static void* mmapwhole(const char* name, long* len)
{
	int fd;
	long ret;
	struct stat st;

	if((fd = sysopen(name, O_RDONLY)) < 0)
		fail("cannot open", name, fd);

	if((ret = sysfstat(fd, &st)) < 0)
		fail("cannot stat", name, ret);

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	ret = sysmmap(NULL, st.st_size, prot, flags, fd, 0);

	if(MMAPERROR(ret))
		fail("cannot mmap", name, ret);

	if(st.st_size > MAX_FILE_SIZE)
		fail("file too large:", name, ret);	

	*len = st.st_size;
	return (void*) ret;
}

static void concat(char* buf, int len, char* a, char* b)
{
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, a);
	p = fmtstr(p, e, b);

	*p++ = '\0';
}

static char* eol(char* p, char* end)
{
	while(p < end && *p != '\n')
		p++;
	return p;
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static void unamer(char* buf, int len)
{
	struct utsname uts;

	xchk(sysuname(&uts), "uname", NULL);

	int relen = strlen(uts.release);

	if(relen > len - 1)
		fail("release name too long:", uts.release, 0);

	memcpy(buf, uts.release, relen + 1);
}

static char* match_depmod_line(char* ls, char* le, char* name, int nlen)
{
	if(*ls == '#')
		return NULL;
	if(*ls == ':')
		return NULL;

	char* sep = strecbrk(ls, le, ':');

	if(sep == le)
		return NULL;

	char* base = strerev(ls, sep, '/');

	if(sep - base < nlen + 3)
		return NULL;
	if(strncmp(base, name, nlen))
		return NULL;
	if(base[nlen] != '.')
		return NULL;

	return sep;
}

static void query_modules_dep(struct modctx* ctx, char* dir, char* name)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->name = name;
	ctx->dir = dir;

	char dep[strlen(dir)+20];
	concat(dep, sizeof(dep), dir, "/modules.dep");

	long dlen = 0;
	char* deps = mmapwhole(dep, &dlen);
	char* dend = deps + dlen;

	char *ls, *le, *sep = NULL;
	int nlen = strlen(name);

	for(ls = le = deps; ls < dend; ls = le + 1) {
		le = eol(ls, dend);
		if((sep = match_depmod_line(ls, le, name, nlen)))
			break;
	} if(ls >= dend) {
		fail("module not found:", name, 0);
	}

	ctx->deps = deps;
	ctx->dlen = dlen;

	ctx->ls = ls;
	ctx->le = le;
	ctx->sep = sep;
}

static void insmod_raw(char* path, char* name, char* pars)
{
	long len;
	void* mod = mmapwhole(path, &len);

	xchk(sysinitmodule(mod, len, pars), "init_module", name);
}

static void insmod_gz(char* path, char* name, char* pars)
{
	fail("gzipped module:", path, 0);
}

static void insmod_xz(char* path, char* name, char* pars)
{
	fail("xz compressed module:", path, 0);
}

static int check_strip_suffix(char* name, int nlen, char* suffix)
{
	int slen = strlen(suffix);

	if(nlen < slen)
		return 0;
	if(strncmp(name + nlen - slen, suffix, slen))
		return 0;

	name[nlen-slen] = '\0';
	return 1;
}

static void insmod(char* path, char* pars)
{
	char* base = (char*)basename(path);
	int blen = strlen(base);
	char name[blen+1];

	memcpy(name, base, blen);
	name[blen] = '\0';

	if(check_strip_suffix(name, blen, ".ko"))
		insmod_raw(path, name, pars);
	else if(check_strip_suffix(name, blen, ".ko.gz"))
		insmod_gz(path, name, pars);
	else if(check_strip_suffix(name, blen, ".ko.xz"))
		insmod_xz(path, name, pars);
	else
		fail("not a module:", path, 0);
};

static void insmod_relative(struct modctx* ctx, char* base, int blen, char* pars)
{
	char* dir = ctx->dir;
	int dlen = strlen(dir);

	char path[dlen + blen + 4];
	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtstrn(p, e, dir, dlen);
	*p++ = '/';
	p = fmtstrn(p, e, base, blen);
	*p++ = '\0';

	insmod(path, pars);
}

static void insmod_dependencies(struct modctx* ctx)
{
	char* p = ctx->sep + 1;
	char* e = ctx->le;

	while(p < e && isspace(*p)) p++;

	while(p < e) {
		char* q = p;

		while(q < e && !isspace(*q)) q++;

		insmod_relative(ctx, p, q - p, NULL);

		while(q < e &&  isspace(*q)) q++;

		p = q;
	}
}

static void insmod_primary_module(struct modctx* ctx, char* pars)
{
	insmod_relative(ctx, ctx->ls, ctx->sep - ctx->ls, pars);
}

static void modprobe(char* name, char* pars)
{
	struct modctx ctx;
	char rel[MAX_RELEASE_LEN];

	unamer(rel, sizeof(rel));

	char dir[strlen(rel)+16];
	concat(dir, sizeof(dir), "/lib/modules/", rel);

	query_modules_dep(&ctx, dir, name);

	insmod_dependencies(&ctx);
	insmod_primary_module(&ctx, pars);
}

static void rmmod(char* name)
{
	xchk(sysdeletemodule(name, 0), name, NULL);
}

int main(int argc, char** argv)
{
	int opts = 0;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i >= argc)
		fail("module name required", NULL, 0);

	if(opts & OPT_r) {
		for(; i < argc; i++)
			rmmod(argv[i]);
	} else {
		char* module = argv[i++];

		argc -= i;
		argv += i;

		int parlen = argsumlen(argc, argv) + argc;
		char* pars = alloca(parlen + 1);
		char* pend = argsmerge(pars, pars + parlen, argc, argv);
		*pend = '\0';

		if(looks_like_path(module))
			insmod(module, pars);
		else
			modprobe(module, pars);
	};

	return 0;
}
