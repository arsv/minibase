#include <sys/file.h>
#include <sys/stat.h>
#include <sys/dents.h>
#include <sys/brk.h>

#include <string.h>
#include <output.h>
#include <util.h>
#include <fail.h>

ERRTAG = "ls";
ERRLIST = {
	REPORT(EACCES), REPORT(EDQUOT), REPORT(EEXIST), REPORT(EFAULT),
	REPORT(EFBIG), REPORT(EINTR), REPORT(EINVAL), REPORT(EISDIR),
	REPORT(ELOOP), REPORT(EMFILE), REPORT(ENFILE), REPORT(ENAMETOOLONG),
	REPORT(ENODEV), REPORT(ENOENT), REPORT(ENOTDIR), REPORT(EOVERFLOW),
	REPORT(EPERM), REPORT(EBADF), REPORT(ENOMEM),
	RESTASNUMBERS
};

#define PAGE 4096

#define MAYBEDIR  0
#define MUSTBEDIR 1

#define DT_LNK_DIR 71	/* symlink pointing to a dir, custom value */

#define OPTS "aubreyw"
#define OPT_a (1<<0)	/* show all files, including hidden ones */
#define OPT_u (1<<1)	/* uniform listing, dirs and filex intermixed */
#define OPT_b (1<<2)	/* basename listing, do not prepend argument */
#define OPT_r (1<<3)	/* recurse into subdirectories */
#define OPT_e (1<<4)	/* list leaf entries (non-dirs) only */
#define OPT_y (1<<5)	/* list symlinks as files, regardless of target */
#define OPT_w (1<<6)	/* follow symlinks */

#define SET_stat (1<<16) /* do stat() entries */

struct dataseg {
	void* base;
	void* ptr;
	void* end;
};

struct idxent {
	struct dirent* de;
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
	void* brk = (void*)xchk(sys_brk(0), "brk", NULL);
	void* end = (void*)xchk(sys_brk(brk + PAGE), "brk", NULL);

	if(brk >= end)
		fail("cannot initialize heap", NULL, 0);

	if(opts & OPT_e)
		opts |= SET_stat; /* need type before entering the dir */
	else if(!(opts & (OPT_r | OPT_u)))
		opts |= SET_stat; /* non-recurse, non-uniform list needs stat */
	else if(opts & OPT_y)
		; /* no need to stat symlinks */
	else
		opts |= SET_stat;

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

	void* old = ds->end;
	void* brk = (void*)xchk(sys_brk(ds->end + ext), "brk", NULL);

	if(brk <= old)
		fail("brk", NULL, 0);

	ds->end = brk;
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

	while((ret = sys_getdents(fd, ds->ptr, ds->end - ds->ptr)) > 0) {
		ds->ptr += ret;
		prepspace(ds, PAGE/2);
	} if(ret < 0)
		fail("cannot read entries from", dir, ret);
}

static int reindex(struct dataseg* ds, void* dents, void* deend)
{
	struct dirent* de;
	void* p;
	int nument = 0;

	for(p = dents; p < deend; nument++, p += de->reclen)
		de = (struct dirent*) p;

	int len = nument * sizeof(struct idxent);
	struct idxent* idx = (struct idxent*) alloc(ds, len);
	struct idxent* end = idx + len;

	for(p = dents; p < deend && idx < end; idx++, p += de->reclen) {
		de = (struct dirent*) p;
		idx->de = de;
	}
	
	return nument;
}

static void statidx(struct idxent* idx, int nument, int fd, int opts)
{
	struct idxent* p;
	struct stat st;
	int flags1 = AT_NO_AUTOMOUNT;
	int flags2 = AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW;

	if(!(opts & SET_stat))
		return;

	for(p = idx; p < idx + nument; p++) {
		int type = p->de->type;

		if(type != DT_UNKNOWN)
			;
		else if(sys_fstatat(fd, p->de->name, &st, flags1) < 0)
			continue;
		else if(S_ISDIR(st.mode))
			type = DT_DIR;
		else if(S_ISLNK(st.mode))
			type = DT_LNK;
		else
			type = DT_REG; /* neither DIR nor LNK nor UNKNOWN */

		p->de->type = type;

		if(type != DT_LNK)
			continue;
		if(opts & OPT_y)
			continue;

		if(sys_fstatat(fd, p->de->name, &st, flags2) < 0)
			continue;
		if(S_ISLNK(st.mode))
			p->de->type = DT_LNK_DIR;
	}
}

static int isdirtype(int t)
{
	return (t == DT_DIR || t == DT_LNK_DIR);
}

static int cmpidx(struct idxent* a, struct idxent* b, int opts)
{
	if(!(opts & OPT_u)) {
		int dira = isdirtype(a->de->type);
		int dirb = isdirtype(b->de->type);

		if(dira && !dirb)
			return -1;
		if(dirb && !dira)
			return  1;
	}
	return strcmp(a->de->name, b->de->name);
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
		const char* name, int strict)
{
	int namelen = strlen(name);
	struct dataseg* ds = &(tc->ds);
	char* fullname = (char*)alloc(ds, dc->len + 1 + namelen + 1);
	char* p = fullname;

	memcpy(p, dc->dir, dc->len); p += dc->len;
	if(p > fullname && *(p-1) != '/') *p++ = '/';
	memcpy(p, name, namelen); p += namelen; *p++ = '\0';

	list(tc, fullname, fullname, strict);
}

static void dumpentry(struct topctx* tc, struct dirctx* dc, struct dirent* de)
{
	struct bufout* bo = &(tc->bo);
	char* name = de->name;
	char type = de->type;

	if(dc->len)
		bufout(bo, dc->dir, dc->len);

	bufout(bo, name, strlen(name));

	if(type == DT_LNK_DIR)
		bufout(bo, "//", 2);
	else if(type == DT_DIR)
		bufout(bo, "//", 1);

	bufout(bo, "\n", 1);
}

static void dumplist(struct topctx* tc, struct dirctx* dc,
		struct idxent* idx, int nument)
{
	struct idxent* p;
	int opts = tc->opts;

	for(p = idx; p < idx + nument; p++) {
		char* name = p->de->name;
		char type = p->de->type;

		if(*name == '.' && !(opts & OPT_a))
			continue;
		if(dotddot(name))
			continue;

		if(type == DT_DIR && (opts & OPT_e))
			;
		else
			dumpentry(tc, dc, p->de);

		if(!(opts & OPT_r))
			continue;
		if(type == DT_DIR)
			recurse(tc, dc, name, MUSTBEDIR);
		else if(type == DT_LNK_DIR && (opts & OPT_w))
			recurse(tc, dc, name, MUSTBEDIR);
		else if(type == DT_UNKNOWN)
			recurse(tc, dc, name, MAYBEDIR);
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
	int flags = O_RDONLY | O_DIRECTORY;

	if(!strict && !(opts & OPT_w))
		flags |= O_NOFOLLOW;

	if(tc->fd >= 0)
		sys_close(tc->fd); /* delayed close */

	if((tc->fd = sys_open(realpath, flags)) < 0) {
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
