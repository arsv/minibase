#include <sys/lstat.h>
#include <sys/getdents.h>
#include <sys/write.h>
#include <sys/open.h>
#include <sys/close.h>

#include <argbits.h>
#include <strlen.h>
#include <memcpy.h>
#include <fail.h>
#include <fmtint64.h>
#include <fmtstr.h>
#include <fmtchar.h>
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

static char* fmt4is(char* p, char* e, int n)
{
	if(p + 3 >= e) return e;

	*(p+3) = '0' + (n % 10); n /= 10;
	*(p+2) = n ? ('0' + n % 10) : ' '; n /= 10;
	*(p+1) = n ? ('0' + n % 10) : ' '; n /= 10;
	*(p+0) = n ? ('0' + n % 10) : ' ';

	return p + 4;
}

static char* fmt1i0(char* p, char* e, int n)
{
	if(p < e) *p++ = '0' + (n % 10); return p;
}

static char* fmtsize(char* p, char* e, uint64_t n)
{
	static const char sfx[] = "KMGTP";
	int sfi = 0;
	int fr = 0;

	/* find out the largest multiplier we can use */
	for(; sfi < sizeof(sfx) && n > 1024; sfi++) {
		fr = n % 1024;
		n /= 1024;
	}
	
	if(sfi >= sizeof(sfx)) {
		/* it's too large; format the number and be done with it */
		p = fmtu64(p, e, n);
		p = fmtchar(p, e, sfx[sizeof(sfx)-1]);
	} else {
		/* it's manageable; do nnnn.d conversion */
		fr = fr*10/1024; /* one decimal */
		p = fmt4is(p, e, n);
		p = fmtchar(p, e, '.');
		p = fmt1i0(p, e, fr);
		p = fmtchar(p, e, sfx[sfi]);
	}

	return p;
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
	
	p = fmtsize(p, e, count >> 1);

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

	*size += st.st_blocks;

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

	*size += st.st_blocks;
	
	if((st.st_mode & S_IFMT) == S_IFDIR)
		scandir(size, path, opts);
}

static int sizecmp(const struct entsize* a, const struct entsize* b, int opts)
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

	qsort(res, argc, sizeof(*res), (qcmp)sizecmp, (void*)(long)opts);

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
