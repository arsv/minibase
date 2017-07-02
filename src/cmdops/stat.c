#include <sys/lstat.h>
#include <sys/open.h>
#include <sys/fstat.h>
#include <sys/mmap.h>
#include <bits/stmode.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <time.h>
#include <fail.h>

#define STATBUF 1024

#define OPTS "tpsugkdiamcbrn"
#define OPT_t (1<<0)   /* type */
#define OPT_p (1<<1)   /* permissions aka mode */
#define OPT_s (1<<2)   /* size */
#define OPT_u (1<<3)   /* uid */
#define OPT_g (1<<4)   /* gid */
#define OPT_k (1<<5)   /* links */
#define OPT_d (1<<6)   /* device id */
#define OPT_i (1<<7)   /* inode */
#define OPT_a (1<<8)   /* atime */
#define OPT_m (1<<9)   /* mtime */
#define OPT_c (1<<10)  /* ctime */
#define OPT_b (1<<11)  /* 512-blocks */
#define OPT_r (1<<12)  /* represented device */
#define OPT_n (1<<13)  /* numeric output */
#define SET_1 (1<<30)

ERRTAG = "stat";
ERRLIST = {
	REPORT(ENOENT), REPORT(EACCES), REPORT(ELOOP), REPORT(EFAULT),
	REPORT(EBADF), REPORT(ENAMETOOLONG), REPORT(ENOENT), REPORT(ENOMEM),
	REPORT(ENOTDIR), REPORT(EOVERFLOW), RESTASNUMBERS
};

/* fmtall is a human-readable form, so it should take "readable" over "complete"
   or "lossless" whenever possible. In case e.g. the exact size of the file 
   in bytes is needed, relevant options like -s should be used.

   Sample output:

   File -rw-rw-r-- alex:alex   1KB  2016-02-02 01:51:45   filename
 
 */

static const char* typemark(struct stat* st)
{
	switch(st->st_mode & S_IFMT) {
		case S_IFREG: return "File";
		case S_IFDIR: return "Dir";
		case S_IFLNK: return "Link";
		case S_IFBLK: return "Block";
		case S_IFCHR: return "Char";
		case S_IFSOCK: return "Socket";
		case S_IFIFO: return "Pipe";
		default: return "?";
	}
}

static char* fmtmode(char* p, char* e, struct stat* st)
{
	char m[6];
	const char digits[] = "01234567";
	int mode = st->st_mode;

	m[5] = '\0';
	m[4] = digits[mode%8]; mode /= 8;
	m[3] = digits[mode%8]; mode /= 8;
	m[2] = digits[mode%8]; mode /= 8;
	m[1] = digits[mode%8]; mode /= 8;
	m[0] = '0';

	char* mstr = (m[1] == '0' ? m + 1 : m);

	return fmtstr(p, e, mstr);
}

static char* fmtperm(char* p, char* e, struct stat* st)
{
	char m[12];
	int mode = st->st_mode;

	m[0] = (mode & S_ISVTX) ? 't' : '-';

	m[1] = (mode & S_IRUSR) ? 'r' : '-';
	m[2] = (mode & S_IWUSR) ? 'w' : '-';
	m[3] = (mode & S_IXUSR) ? 'x' : '-';

	m[4] = (mode & S_IRGRP) ? 'r' : '-';
	m[5] = (mode & S_IWGRP) ? 'w' : '-';
	m[6] = (mode & S_IXGRP) ? 'x' : '-';

	m[7] = (mode & S_IROTH) ? 'r' : '-';
	m[8] = (mode & S_IWOTH) ? 'w' : '-';
	m[9] = (mode & S_IXOTH) ? 'x' : '-';

	m[10] = '\0';

	return fmtstr(p, e, m);
}

static char* fmtdev(char* p, char* e, uint64_t dev)
{
	int min = ((dev >>  0) & 0x000000FF)
	        | ((dev >> 12) & 0xFFFFFF00);
	int maj = ((dev >>  8) & 0x00000FFF)
	        | ((dev >> 32) & 0xFFFFF000);

	p = fmti32(p, e, maj);
	p = fmtstr(p, e, ":");
	p = fmti32(p, e, min);

	return p;
}

static char* fmtmodeperm(char* p, char* e, struct stat* st, int opts)
{
	if(opts & OPT_n)
		return fmtmode(p, e, st);
	else
		return fmtperm(p, e, st);
}

static char* sumtypemode(char* p, char* e, struct stat* st, int opts)
{
	p = fmtstr(p, e, typemark(st));
	p = fmtstr(p, e, " ");

	if(st->st_rdev) {
		p = fmtstr(p, e, "dev ");
		p = fmtdev(p, e, st->st_rdev);
		p = fmtstr(p, e, " ");
	}

	p = fmtmodeperm(p, e, st, opts);

	return p;
}

/* Showing human-readable user and group names requires parsing
   /etc/passwd and /etc/group. This is done by mmaping them whole,
   as the files are presumed to be small.
   
   The files are not unmmaped, and we do not even bother closing fds
   since stat exits mere moments after printing the info. The data
   is not shared either because this stat implementation always
   handles exactly one file. */

static char* mmapfile(const char* fname, long* size)
{
	long fd = sysopen(fname, O_RDONLY);
	if(fd < 0) return NULL;

	struct stat st;
	long sr = sysfstat(fd, &st);
	if(sr < 0) return NULL;

	if(st.st_size > *size) return NULL;

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	long mr = sysmmap(NULL, st.st_size, prot, flags, fd, 0);
	if(MMAPERROR(mr)) return NULL;

	*size = st.st_size;
	return (char*)mr;
}

static char* idname(char* name, char* nend, int id, const char* dictname)
{
	char idstr[20];
	char* idend = idstr + sizeof(idstr) - 1;
	char* idptr = fmti32(idstr, idend, id);
	*idptr++ = ':';
	int idlen = idptr - idstr;

	if(!dictname) goto asnum;
	
	long size = 0x7FFFFFFF; /* max size of /etc/passwd */
	char* buf = mmapfile(dictname, &size);
	char* end = buf + size;

	if(!buf) goto asnum;
	
	char *ls, *le; /* line start/end */
	for(ls = buf; ls < end; ls = le + 1) {
		le = strecbrk(ls, end, '\n');

		char* ns = ls;                     /* 1st field, name */
		char* ne = strecbrk(ls, le, ':');
		char* ps = ne + 1;                 /* 2nd field, password */
		char* pe = strecbrk(ps, le, ':');
		char* is = pe + 1;                 /* 3rd field, id */
		char* ie = strecbrk(is, le, ':');

		if(ie >= le)
			continue;
		if(strncmp(idstr, is, idlen))
			continue;

		return fmtstrn(name, nend, ns, ne - ns);
	}
asnum:
	return fmtstrn(name, nend, idstr, idlen - 1);
}

static char* sumownergrp(char* p, char* e, struct stat* st, int opts)
{
	/* limit the length of user:group string */
	char* ne = p + 50 < e ? p + 50 : e;
	int resolve = (opts & OPT_n ? 0 : 1);

	p = fmtstr(p, e, " ");
	p = idname(p, ne, st->st_uid, resolve ? "/etc/passwd" : NULL);
	p = fmtstr(p, e, ":");
	p = idname(p, ne, st->st_gid, resolve ? "/etc/group" : NULL);
	
	return p;
}

static char* sumsize(char* p, char* e, struct stat* st, int opts)
{
	int mode = st->st_mode;

	if(!st->st_size && !(S_ISLNK(mode) || S_ISREG(mode)))
		goto out;

	p = fmtstr(p, e, " ");
	p = fmtu64(p, e, st->st_size);
	p = fmtstr(p, e, st->st_size == 1 ? " byte" : " bytes");

	if(st->st_size < 10000 || (opts & OPT_n)) goto out;

	p = fmtstr(p, e, " (");
	p = fmtsize(p, e, st->st_size);
	p = fmtstr(p, e, ")");
out:
	return p;
}

static char* sumlinks(char* p, char* e, struct stat* st)
{
	if(st->st_nlink == 1) return p;

	p = fmtstr(p, e, " ");
	p = fmtlong(p, e, st->st_nlink);
	p = fmtstr(p, e, " links");

	return p;
}

static char* sumblkdevino(char* p, char* e, struct stat* st, int opts)
{
	int du = 512*st->st_blocks;
	int diff = (du > st->st_size + 512 || du < st->st_size - 512);

	if(!st->st_blocks) {
		p = fmtstr(p, e, "Resides");
	} else {
		p = fmtstr(p, e, "Takes ");
		p = fmtu64(p, e, st->st_blocks);
		p = fmtstr(p, e, st->st_blocks > 1 ? " blocks" : " block");
	} if(du >= 10000 && diff && !(opts & OPT_n)) {
		p = fmtstr(p, e, " (");
		p = fmtsize(p, e, 512*st->st_blocks);
		p = fmtstr(p, e, ")");
	}

	p = fmtstr(p, e, " on dev ");
	p = fmtdev(p, e, st->st_dev);
	p = fmtstr(p, e, " inode ");
	p = fmtu64(p, e, st->st_ino);
	
	return p;
}

static char* sumtime(char* p, char* e, char* tag, long* ts)
{
	struct tm tm;
	struct timeval tv = { .sec = *ts, .usec = 0 };
	tv2tm(&tv, &tm);

	p = fmtstr(p, e, tag);
	p = fmtstr(p, e, " ");
	p = fmtpad(p, e, 10, fmtlong(p, e, *ts));
	p = fmtstr(p, e, " ");
	p = fmttm(p, e, &tm);

	return p;
}

static char* summary(char* p, char* e, struct stat* st, int opts)
{
	p = sumtypemode(p, e, st, opts);
	p = sumownergrp(p, e, st, opts);
	p = sumsize(p, e, st, opts);
	p = sumlinks(p, e, st);

	p = fmtstr(p, e, "\n");
	p = sumblkdevino(p, e, st, opts);

	p = fmtstr(p, e, "\n");
	p = sumtime(p, e, "Modify time", &(st->st_mtime));
	p = fmtstr(p, e, "\n");
	p = sumtime(p, e, "Access time", &(st->st_atime));

	return p;
}

static char* formtime(char* p, char* e, long ts, int opts)
{
	if(opts & OPT_n)
		return fmtlong(p, e, ts);

	struct tm tm;
	struct timeval tv = { .sec = ts, .usec = 0 };
	tv2tm(&tv, &tm);

	return fmttm(p, e, &tm);
}

static char* formowner(char* p, char* e, long id, int opts, const char* dict)
{
	return idname(p, e, id, (opts & OPT_n) ? NULL : dict);
}

static char* format(char* p, char* e, struct stat* st, int opts)
{
	if(opts & OPT_t)
		return fmtstr(p, e, typemark(st));
	if(opts & OPT_p)
		return fmtmodeperm(p, e, st, opts);
	if(opts & OPT_s)
		return fmtu64(p, e, st->st_size);
	if(opts & OPT_u)
		return formowner(p, e, st->st_uid, opts, "/etc/passwd");
	if(opts & OPT_g)
		return formowner(p, e, st->st_gid, opts, "/etc/group");
	if(opts & OPT_k)
		return fmtu64(p, e, st->st_nlink);
	if(opts & OPT_a)
		return formtime(p, e, st->st_atime, opts);
	if(opts & OPT_m)
		return formtime(p, e, st->st_mtime, opts);
	if(opts & OPT_c)
		return formtime(p, e, st->st_ctime, opts);
	if(opts & OPT_d)
		return fmtdev(p, e, st->st_dev);
	if(opts & OPT_i)
		return fmtu64(p, e, st->st_ino);
	if(opts & OPT_b)
		return fmtu64(p, e, st->st_blocks);
	if(opts & OPT_r)
		return fmtdev(p, e, st->st_rdev);
	else
		fail("unhandled option", NULL, 0);
}

static void stat(const char* name, int opts)
{
	struct stat st;

	xchk(syslstat(name, &st), NULL, name);

	int len = 512;
	char buf[len];
	char* end = buf + sizeof(buf) - 1;
	char* p = buf;

	if(opts & SET_1)
		p = format(buf, end, &st, opts);
	else
		p = summary(buf, end, &st, opts);

	*p++ = '\n';

	xchk(writeall(STDOUT, buf, p - buf), "write", NULL);
}

static int countbits(int val, int max)
{
	int i;
	int cnt = 0;

	for(i = 0; i < max; i++)
		if(val & (1<<i))
			cnt++;

	return cnt;
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(i >= argc)
		fail("file name expected", NULL, 0);
	else if(i < argc - 1)
		fail("one file at a time please", NULL, 0);

	int cb = countbits(opts, 13);
	if(cb > 1)
		fail("too many options", NULL, 0);
	else if(cb)
		opts |= SET_1;

	stat(argv[i], opts);

	return 0;
}
