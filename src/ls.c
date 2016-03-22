#include <sys/open.h>
#include <sys/getdents.h>
#include <sys/fstatat.h>
#include <sys/brk.h>
#include <sys/close.h>
#include <bits/stmode.h>

#include <argbits.h>
#include <bufout.h>
#include <strlen.h>
#include <strcmp.h>
#include <memcpy.h>
#include <qsort.h>
#include <fail.h>

ERRTAG = "ls";
ERRLIST = {
	REPORT(ENOENT), REPORT(ENOTDIR), REPORT(EFAULT),
	RESTASNUMBERS
};

#define PAGE 4096

#define MAYBEDIR  0
#define MUSTBEDIR 1

#define OPTS "aubr"
#define OPT_a (1<<0)	/* show all files, including hidden ones */
#define OPT_u (1<<1)	/* uniform listing, dirs and filex intermixed */
#define OPT_b (1<<2)	/* basename listing, do not prepend argument */
#define OPT_r (1<<3)	/* recurse into subdirectories */

struct dataseg {
	void* base;
	void* ptr;
	void* end;
};

struct idxent {
	struct dirent64* de;
};

struct topctx {
	int opts;
	int fd;
	struct dataseg ds;
	struct bufout bo;
};

struct dirctx {
	char* dir;
	int len;
};

char output[PAGE];

static void init(struct topctx* tc, int opts)
{
	void* brk = (void*)xchk(sysbrk(0), "brk", NULL);
	void* end = (void*)xchk(sysbrk(brk + PAGE), "brk", NULL);

	tc->fd = -1;
	tc->opts = opts;

	tc->ds.base = brk;
	tc->ds.end = end;
	tc->ds.ptr = brk;

	tc->bo.fd = 1;
	tc->bo.buf = output;
	tc->bo.len = sizeof(output);
	tc->bo.ptr = 0;
}

static void fini(struct topctx* tc)
{
	bufoutflush(&(tc->bo));
}

static void prepspace(struct dataseg* ds, long ext)
{
	if(ds->ptr + ext < ds->end)
		return;
	if(ext % PAGE)
		ext += PAGE - (ext % PAGE);

	ds->end = (void*)xchk(sysbrk(ds->end + ext), "brk", NULL);
}

static void* alloc(struct dataseg* ds, int len)
{
	prepspace(ds, len);
	char* ret = ds->ptr;
	ds->ptr += len;
	return ret;
}

static void readwhole(struct dataseg* ds, int fd, const char* dir)
{
	long ret;

	while((ret = sysgetdents64(fd, ds->ptr, ds->end - ds->ptr)) > 0) {
		ds->ptr += ret;
		prepspace(ds, PAGE/2);
	} if(ret < 0)
		fail("cannot read entries from", dir, ret);
}

static int reindex(struct dataseg* ds, void* dents, void* deend)
{
	struct dirent64* de;
	void* p;
	int nument = 0;

	for(p = dents; p < deend; nument++, p += de->d_reclen)
		de = (struct dirent64*) p;

	int len = nument * sizeof(struct idxent);
	struct idxent* idx = (struct idxent*) alloc(ds, len);
	struct idxent* end = idx + len;

	for(p = dents; p < deend && idx < end; idx++, p += de->d_reclen) {
		de = (struct dirent64*) p;
		idx->de = de;
	}
	
	return nument;
}

static void statidx(struct idxent* idx, int nument, int fd, int opts)
{
	struct idxent* p;
	struct stat st;
	const int flags = AT_NO_AUTOMOUNT;

	if(opts & (OPT_u | OPT_r))
		return; /* no need to stat anything for uniform lists */

	for(p = idx; p < idx + nument; p++) {
		if(p->de->d_type != DT_UNKNOWN)
			continue;

		if(sysfstatat(fd, p->de->d_name, &st, flags) < 0)
			continue;

		if(S_ISDIR(st.st_mode))
			p->de->d_type = DT_DIR;
		else
			p->de->d_type = DT_REG; /* neither DIR nor UNKNOWN */
	}
}

static int cmpidx(struct idxent* a, struct idxent* b, int opts)
{
	if(!(opts & OPT_u)) {
		int dira = (a->de->d_type == DT_DIR);
		int dirb = (b->de->d_type == DT_DIR);

		if(dira && !dirb)
			return -1;
		if(dirb && !dira)
			return  1;
	}
	return strcmp(a->de->d_name, b->de->d_name);
}

static void sortidx(struct idxent* idx, int nument, int opts)
{
	qsort(idx, nument, sizeof(*idx), (qcmp)cmpidx, opts);
}

static int dotddot(const char* name)
{
	if(name[0] != '.') return 0;
	if(name[1] == '\0') return 1;
	if(name[1] != '.') return 0;
	if(name[2] == '\0') return 1;
	return 0;
}

static void list(struct topctx* tc, const char* realpath, const char* showpath, int strict);

static void recurse(struct topctx* tc, struct dirctx* dc,
		const char* name, int len, int strict)
{
	struct dataseg* ds = &(tc->ds);
	char* fullname = (char*)alloc(ds, dc->len + 1 + len + 1);
	char* p = fullname;

	memcpy(p, dc->dir, dc->len); p += dc->len;
	if(p > fullname && *(p-1) != '/') *p++ = '/';
	memcpy(p, name, len); p += len; *p++ = '\0';

	list(tc, fullname, fullname, strict);
}

static void dumplist(struct topctx* tc, struct dirctx* dc,
		struct idxent* idx, int nument)
{
	struct idxent* p;
	struct bufout* bo = &(tc->bo);
	int opts = tc->opts;

	for(p = idx; p < idx + nument; p++) {
		char* name = p->de->d_name;
		char type = p->de->d_type;

		if(*name == '.' && !(opts & OPT_a))
			continue;
		if(dotddot(name))
			continue;

		int namelen = strlen(name);

		if(dc->len)
			bufout(bo, dc->dir, dc->len);

		bufout(bo, name, namelen);

		if(type == DT_DIR)
			bufout(bo, "/", 1);

		bufout(bo, "\n", 1);

		if(!(opts & OPT_r))
			continue;
		if(type == DT_DIR)
			recurse(tc, dc, name, namelen, MUSTBEDIR);
		else if(type == DT_UNKNOWN)
			recurse(tc, dc, name, namelen, MAYBEDIR);
	}
}

static void makedirctx(struct topctx* tc, struct dirctx* dc, const char* path)
{
	if(path) {
		struct dataseg* ds = &(tc->ds);
		int len = strlen(path);
		char* buf = (char*)alloc(ds, len + 2);
		memcpy(buf, path, len);
		if(len && buf[len-1] != '/')
			buf[len++] = '/';
		buf[len] = '\0';
		dc->dir = buf;
		dc->len = len;
	} else {
		dc->dir = NULL;
		dc->len = 0;
	}
}

static void list(struct topctx* tc, const char* realpath, const char* showpath, int strict)
{
	struct dataseg* ds = &(tc->ds);
	struct dirctx dc;
	int opts = tc->opts;

	if(tc->fd >= 0) sysclose(tc->fd); /* delayed close */

	if((tc->fd = sysopen(realpath, O_RDONLY | O_DIRECTORY)) < 0) {
		if(strict && tc->fd == ENOTDIR)
			return;
		else
			fail("cannot open", realpath, tc->fd);
	}

	void* oldptr = ds->ptr;		/* start ds frame */

	makedirctx(tc, &dc, showpath);

	void* dents = ds->ptr;
	readwhole(ds, tc->fd, realpath);
	void* deend = ds->ptr;

	int nument = reindex(ds, dents, deend);
	struct idxent* idx = (struct idxent*) deend;

	statidx(idx, nument, tc->fd, opts);
	sortidx(idx, nument, opts);
	dumplist(tc, &dc, idx, nument);

	ds->ptr = oldptr;		/* end ds frame */
}

int main(int argc, char** argv)
{
	struct topctx tc;
	int opts = 0;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	init(&tc, opts);

	if(i >= argc)
		list(&tc, ".", NULL, MUSTBEDIR);
	else for(; i < argc; i++)
		list(&tc, argv[i], (opts & OPT_b) ? NULL : argv[i], MUSTBEDIR);

	fini(&tc);

	return 0;
}
