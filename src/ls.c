#include <sys/open.h>
#include <sys/getdents.h>
#include <sys/fstatat.h>
#include <sys/brk.h>
#include <bits/stmode.h>

#include <argbits.h>
#include <bufout.h>
#include <strlen.h>
#include <strcmp.h>
#include <qsort.h>
#include <fail.h>

ERRTAG = "ls";
ERRLIST = {
	REPORT(ENOENT), REPORT(ENOTDIR), REPORT(EFAULT),
	RESTASNUMBERS
};

#define PAGE 4096

#define OPTS "au"
#define OPT_a (1<<0)	/* show all files, including hidden ones */
#define OPT_u (1<<1)	/* uniform listing, dirs and filex intermixed */

struct dataseg {
	void* base;
	void* ptr;
	void* end;
};

struct idxent {
	struct dirent64* de;
};

char output[PAGE];

static void datainit(struct dataseg* ds)
{
	ds->base = (void*)xchk(sysbrk(0), "brk", NULL);
	ds->end = (void*)xchk(sysbrk(ds->base + PAGE), "brk", NULL);
	ds->ptr = ds->base;
}

static void datagrow(struct dataseg* ds, long ext)
{
	if(ext % PAGE) ext += PAGE - (ext % PAGE);
	ds->end = (void*)xchk(sysbrk(ds->end + ext), "brk", NULL);
}

static void readwhole(struct dataseg* ds, int fd, const char* dir)
{
	long ret;

	while((ret = sysgetdents64(fd, ds->ptr, ds->end - ds->ptr)) > 0) {
		ds->ptr += ret;
		if(ds->end - ds->ptr < PAGE/2)
			datagrow(ds, PAGE);
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
	if(ds->end - ds->ptr < len)
		datagrow(ds, len);
	
	struct idxent* idx = (struct idxent*) ds->ptr;
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

	if(opts & OPT_u)
		return; /* no need to stat anything for uniform lists */

	for(p = idx; p < idx + nument; p++) {
		if(p->de->d_type != DT_UNKNOWN)
			continue;

		if(sysfstatat(fd, p->de->d_name, &st, flags) < 0)
			continue;

		if(S_ISDIR(st.st_mode))
			p->de->d_type = DT_DIR;
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

static void dumplist(struct idxent* idx, int nument, int opts)
{
	struct bufout bo = {
		.fd = 1,
		.buf = output,
		.len = sizeof(output),
		.ptr = 0
	};
	struct idxent* p;

	for(p = idx; p < idx + nument; p++) {
		char* name = p->de->d_name;
		char type = p->de->d_type;

		if(*name == '.' && !(opts & OPT_a))
			continue;
		if(dotddot(name))
			continue;

		bufout(&bo, name, strlen(name));
		if(type == DT_DIR)
			bufout(&bo, "/", 1);
		bufout(&bo, "\n", 1);
	}

	bufoutflush(&bo);
}

static void list(struct dataseg* ds, const char* path, int opts)
{
	const char* realpath = path ? path : ".";
	long fd = xchk(sysopen(realpath, O_RDONLY | O_DIRECTORY),
			NULL, realpath);

	void* dents = ds->ptr;

	readwhole(ds, fd, realpath);

	void* deend = ds->ptr;

	int nument = reindex(ds, dents, deend);

	struct idxent* idx = (struct idxent*) deend;

	statidx(idx, nument, fd, opts);

	sortidx(idx, nument, opts);

	dumplist(idx, nument, opts);
}

int main(int argc, char** argv)
{
	int opts = 0;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	struct dataseg ds;
	datainit(&ds);

	if(i >= argc)
		list(&ds, NULL, opts);
	else for(; i < argc; i++)
		list(&ds, argv[i], opts);

	return 0;
}
