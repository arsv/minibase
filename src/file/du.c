#include <sys/lstat.h>
#include <sys/getdents.h>
#include <sys/write.h>
#include <sys/open.h>
#include <sys/close.h>

#include <argbits.h>
#include <strlen.h>
#include <memcpy.h>
#include <fail.h>
#include <fmtstr.h>
#include <fmtpad.h>
#include <fmtint64.h>
#include <fmtsize.h>
#include <fmtint64.h>
#include <qsort.h>
#include <strcmp.h>

#define OPTS "scbn"
#define OPT_s (1<<0)
#define OPT_c (1<<1)
#define OPT_b (1<<2)
#define OPT_n (1<<3)

ERRTAG = "du";
ERRLIST = {
	REPORT(ENOENT), RESTASNUMBERS
};

struct entsize {
	uint64_t size;
	char* name;
};

static void addstsize(uint64_t* sum, struct stat* st, int opts)
{
	if(opts & OPT_b)
		*sum += st->st_size;
	else
		*sum += st->st_blocks*512;
}

/* No buffering here. For each line written, there will be at least one
   stat() call, probably much more, and the user would probably like to
   see some progress while the disc is being scanned. */

static void dump(uint64_t count, char* tag, int opts)
{
	int taglen = tag ? strlen(tag) : 0;

	char buf[100 + taglen];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;
	
	if(opts & OPT_n)
		p = fmtpad(p, e, 8, fmtu64(p, e, count));
	else
		p = fmtpad(p, e, 5, fmtsize(p, e, count));

	if(tag) {
		p = fmtstr(p, e, "  ");
		p = fmtstr(p, e, tag);
	};

	*p++ = '\n';
	
	char* q = buf;
	if(!(opts & OPT_s))
		while(*q == ' ') q++;

	syswrite(1, q, p - q);
}

static inline int dotddot(const char* p)
{
	if(!p[0])
		return 1;
	if(p[0] == '.' && !p[1])
		return 1;
	if(p[1] == '.' && !p[2])
		return 1;
	return 0;
}

static void scandir(uint64_t* size, char* path, int opts);

static void scanent(uint64_t* size, char* path, char* name, int opts)
{
	int pathlen = strlen(path);
	int namelen = strlen(name);
	char fullname[pathlen + namelen + 2];

	char* p = fullname;
	memcpy(p, path, pathlen); p += pathlen; *p++ = '/';
	memcpy(p, name, namelen); p += namelen; *p = '\0';

	struct stat st;

	long ret = syslstat(fullname, &st);
	if(ret < 0)
		return;

	addstsize(size, &st, opts);

	if((st.st_mode & S_IFMT) != S_IFDIR)
		return;

	scandir(size, fullname, opts);
}

static void scandir(uint64_t* size, char* path, int opts)
{
	long fd = sysopen(path, O_RDONLY | O_DIRECTORY);

	if(fd < 0)
		fail("cannot open", path, -fd);

	long rd;
	char buf[1024];

	while((rd = sysgetdents64(fd, (struct dirent64*)buf, sizeof(buf))) > 0) {
		char* ptr = buf;
		char* end = buf + rd;
		while(ptr < end) {
			struct dirent64* de = (struct dirent64*) ptr;

			if(dotddot(de->d_name))
				goto next;
			if(!de->d_reclen)
				break;

			scanent(size, path, de->d_name, opts);

			next: ptr += de->d_reclen;
		}
	}

	sysclose(fd);
}

static void scan(uint64_t* size, char* path, int opts)
{
	struct stat st;
	
	xchk(syslstat(path, &st), "cannot stat", path);

	addstsize(size, &st, opts);
	
	if((st.st_mode & S_IFMT) == S_IFDIR)
		scandir(size, path, opts);
}

static int sizecmp(const struct entsize* a, const struct entsize* b)
{
	if(a->size < b->size)
		return -1;
	if(a->size > b->size)
		return  1;
	else
		return strcmp(a->name, b->name);
}

static void scanall(uint64_t* total, int argc, char** argv, int opts)
{
	int i;
	struct entsize res[argc];

	for(i = 0; i < argc; i++) {
		res[i].name = argv[i];
		res[i].size = 0;
		uint64_t* sizep = &(res[i].size);

		scan(sizep, argv[i], opts);
		*total += *sizep;
	}

	if(!(opts & OPT_s))
		return;

	qsort(res, argc, sizeof(*res), (qcmp)sizecmp, NULL);

	for(i = 0; i < argc; i++)
		dump(res[i].size, res[i].name, opts);
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	argc -= i;
	argv += i;

	char* dot =  ".";
	uint64_t total = 0;

	if(opts & OPT_b)
		opts |= OPT_n;

	if(opts & (OPT_s | OPT_c))
		;
	else if(argc >= 2)
		opts |= OPT_s;
	else
		opts |= OPT_c;

	if(argc)
		scanall(&total, argc, argv, opts);
	else 
		scanall(&total, 1, &dot, opts);

	if(opts & OPT_c)
		dump(total, NULL, opts);

	return 0;
}
