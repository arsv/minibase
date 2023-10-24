#include <sys/file.h>
#include <sys/info.h>
#include <sys/time.h>
#include <format.h>
#include <time.h>

#include "shell.h"

/* several commands fetching some status info and formatting it nicely */

static const char* typemark(struct stat* st)
{
	switch(st->mode & S_IFMT) {
		case S_IFREG: return "file";
		case S_IFDIR: return "dir";
		case S_IFLNK: return "link";
		case S_IFBLK: return "block";
		case S_IFCHR: return "char";
		case S_IFSOCK: return "socket";
		case S_IFIFO: return "pipe";
		default: return "?";
	}
}

static char* fmtmode(char* p, char* e, struct stat* st)
{
	char m[6];
	const char digits[] = "01234567";
	int mode = st->mode;

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
	int mode = st->mode;

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

static char* fmtmodeperm(char* p, char* e, struct stat* st)
{
	p = fmtmode(p, e, st);
	p = fmtstr(p, e, " (");
	p = fmtperm(p, e, st);
	p = fmtstr(p, e, ")");

	return p;
}

static char* sumtypemode(char* p, char* e, struct stat* st)
{
	p = fmtstr(p, e, typemark(st));
	p = fmtstr(p, e, " ");

	if(st->rdev) {
		p = fmtstr(p, e, "dev ");
		p = fmtdev(p, e, st->rdev);
		p = fmtstr(p, e, " ");
	}

	p = fmtmodeperm(p, e, st);

	return p;
}

static char* sumownergrp(char* p, char* e, struct stat* st)
{
	p = fmtstr(p, e, " owner ");
	p = fmtint(p, e, st->uid);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, st->gid);

	return p;
}

static char* sumsize(char* p, char* e, struct stat* st)
{
	int mode = st->mode;

	if(!st->size && !(S_ISLNK(mode) || S_ISREG(mode)))
		goto out;

	p = fmtstr(p, e, " size ");
	p = fmtu64(p, e, st->size);

	if(st->size < 10000) goto out;

	p = fmtstr(p, e, " (");
	p = fmtsize(p, e, st->size);
	p = fmtstr(p, e, ")");
out:
	return p;
}

static char* sumlinks(char* p, char* e, struct stat* st)
{
	if(st->nlink == 1) return p;

	p = fmtstr(p, e, " nlinks ");
	p = fmtlong(p, e, st->nlink);

	return p;
}

static char* sumtime(char* p, char* e, char* tag, struct timespec* ts)
{
	struct tm tm;
	struct timeval tv = { .sec = ts->sec, .usec = ts->nsec/1000 };

	tv2tm(&tv, &tm);

	p = fmtstr(p, e, tag);
	p = fmtstr(p, e, " ");
	p = fmtpad(p, e, 10, fmtlong(p, e, ts->sec));
	p = fmtstr(p, e, " ");
	p = fmttm(p, e, &tm);

	return p;
}

static char* summary(char* p, char* e, struct stat* st)
{
	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ": ");

	p = sumtypemode(p, e, st);
	p = sumownergrp(p, e, st);
	p = sumsize(p, e, st);
	p = sumlinks(p, e, st);

	p = fmtstr(p, e, "\n");
	//p = sumblkdevino(p, e, st, opts);

	p = sumtime(p, e, " mtime", &(st->mtime));
	p = fmtstr(p, e, "\n");
	p = sumtime(p, e, " atime", &(st->atime));
	p = fmtstr(p, e, "\n");

	return p;
}

static void format_stat(struct stat* st)
{
	int size = 1024;
	char* buf = alloca(size);

	char* p = buf;
	char* e = buf + size - 1;

	p = summary(p, e, st);

	output(buf, p - buf);
}

void cmd_stat(void)
{
	char* name;
	struct stat st;
	int ret;

	if(!(name = shift_arg()))
		return;
	if(extra_arguments())
		return;

	if((ret = sys_lstat(name, &st)) < 0)
		return repl(NULL, NULL, ret);

	format_stat(&st);
}

static char* fmtpart(char* p, char* e, long n, char* unit)
{
	if(!n) goto out;

	p = fmtstr(p, e, " ");
	p = fmtlong(p, e, n);
	p = fmtstr(p, e, unit);
out:
	return p;
}

static char* fmtuptime(char* p, char* e, long ts)
{
	int sec = ts % 60; ts /= 60;
	int min = ts % 60; ts /= 60;
	int hrs = ts % 24; ts /= 24;

	p = fmtpart(p, e, ts, "d");
	p = fmtpart(p, e, hrs, "h");
	p = fmtpart(p, e, min, "m");
	p = fmtpart(p, e, sec, "s");

	return p;
}

static char* fmtavg(char* p, char* e, unsigned long avg)
{
	int avgperc = (avg >> 16) & 0xFFFF;
	int avgfrac = ((avg & 0xFFFF)*100 >> 16) & 0xFFFF;

	p = fmti32(p, e, avgperc);
	p = fmtstr(p, e, ".");
	p = fmtulp(p, e, avgfrac, 2);

	return p;
}

static char* fmtmem(char* p, char* e, unsigned long n, int mu)
{
	unsigned long nb = n * mu;
	unsigned long fr = 0;
	char* suff = "";

	if(nb > 1024) { suff = "K"; fr = nb % 1024; nb /= 1024; }
	if(nb > 1024) { suff = "M"; fr = nb % 1024; nb /= 1024; }
	if(nb > 1024) { suff = "G"; fr = nb % 1024; nb /= 1024; }

	fr /= 102;

	p = fmtu32(p, e, nb);
	if(fr) {
		p = fmtstr(p, e, ".");
		p = fmtulp(p, e, fr, 2);
	}
	p = fmtstr(p, e, suff);

	return p;
}

static char* fmtline1(char* p, char* e, struct sysinfo* si)
{
	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, "uptime");
	p = fmtuptime(p, e, si->uptime);
	p = fmtstr(p, e, " nprocs ");
	p = fmti32(p, e, si->procs);
	p = fmtstr(p, e, " load ");
	p = fmtavg(p, e, si->loads[0]);
	p = fmtstr(p, e, " ");
	p = fmtavg(p, e, si->loads[1]);
	p = fmtstr(p, e, " ");
	p = fmtavg(p, e, si->loads[2]);
	p = fmtstr(p, e, "\n");

	return p;
}

static char* fmtline3(char* p, char* e, struct sysinfo* si)
{
	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, "RAM ");
	p = fmtmem(p, e, si->totalram, si->mem_unit);
	p = fmtstr(p, e, " free ");
	p = fmtmem(p, e, si->freeram, si->mem_unit);
	p = fmtstr(p, e, " shared ");
	p = fmtmem(p, e, si->sharedram, si->mem_unit);
	p = fmtstr(p, e, " buffers ");
	p = fmtmem(p, e, si->bufferram, si->mem_unit);
	p = fmtstr(p, e, "\n");
	return p;
}

static char* fmtline4(char* p, char* e, struct sysinfo* si)
{
	if(!si->totalswap && !si->totalhigh)
		return p;

	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ": ");

	if(si->totalswap) {
		p = fmtstr(p, e, "swap ");
		p = fmtmem(p, e, si->totalswap, si->mem_unit);
		p = fmtstr(p, e, " (");
		p = fmtmem(p, e, si->freeswap, si->mem_unit);
		p = fmtstr(p, e, " free)");
	} if(si->totalhigh) {
		p = fmtstr(p, e, si->totalswap ? " high " : "High");
		p = fmtmem(p, e, si->totalhigh, si->mem_unit);
		p = fmtstr(p, e, " (");
		p = fmtmem(p, e, si->freehigh, si->mem_unit);
		p = fmtstr(p, e, " free)");
	}
	p = fmtstr(p, e, "\n");

	return p;
}

static void format_sysinfo(struct sysinfo* si)
{
	char buf[1024];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtline1(p, e, si);
	p = fmtline3(p, e, si);
	p = fmtline4(p, e, si);

	output(buf, p - buf);
}

void cmd_info(void)
{
	struct sysinfo si;
	int ret;

	if(extra_arguments())
		return;
	if((ret = sys_info(&si)) < 0)
		return repl("sysinfo", NULL, ret);

	format_sysinfo(&si);
}

void cmd_time(void)
{
	struct timeval tv;
	struct tm tm;
	int ret;

	if((ret = sys_gettimeofday(&tv, NULL)) < 0)
		return repl(NULL, NULL, ret);

	tv2tm(&tv, &tm);

	int size = 200;
	char* buf = alloca(size);
	char* p = buf;
	char* e = buf + size - 1;

	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ": ");
	p = fmtpad(p, e, 10, fmtlong(p, e, tv.sec));
	p = fmtstr(p, e, " ");
	p = fmttm(p, e, &tm);

	*p++ = '\n';

	output(buf, p - buf);
}
