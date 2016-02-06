#include <sys/open.h>
#include <sys/close.h>
#include <sys/getdents.h>
#include <sys/chmod.h>
#include <sys/stat.h>
#include <sys/rmdir.h>
#include <sys/umask.h>
#include <sys/lstat.h>
#include <sys/_exit.h>

#include <argbits.h>
#include <strlen.h>
#include <fmtstr.h>
#include <fmtchar.h>
#include <fail.h>

#define OPTS "rfdn"
#define OPT_r (1<<0)	/* recursively */
#define OPT_f (1<<1)	/* force */
#define OPT_d (1<<2)	/* directories only */
#define OPT_n (1<<3)	/* non-directories only */

#define DEBUFSIZE 2000

ERRTAG = "chmod";
ERRLIST = {
	REPORT(EACCES), REPORT(EBUSY), REPORT(EFAULT), REPORT(EIO),
	REPORT(EISDIR), REPORT(ELOOP), REPORT(ENOENT), REPORT(ENOMEM),
	REPORT(ENOTDIR), REPORT(EPERM), REPORT(EROFS), REPORT(EOVERFLOW),
	REPORT(ENOSYS), REPORT(EBADF), REPORT(EINVAL), RESTASNUMBERS
};

struct chmod {
	int clr;	/* file mode transformation: */
	int set;	/* newmode = (oldmode & ~clr) | set */
	int opts;
};

static void recdent(const char* dirname, struct chmod* ch, struct dirent64* de);
static void chmodst(const char* entname, struct chmod* ch, struct stat* st);

/* With -f we keep going *and* suppress the message.
   Hm. Would be better to report it but keep going, maybe? */

static void mfail(long ret, struct chmod* ch, const char* msg, const char* obj)
{
	if(ch->opts & OPT_f)
		return;
	warn(msg, obj, ret);
	_exit(-1);
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

static void recurse(const char* dirname, struct chmod* ch)
{
	char debuf[DEBUFSIZE];
	const int delen = sizeof(debuf);
	long rd;

	long dirfd = sysopen(dirname, O_DIRECTORY);

	if(dirfd < 0)
		return mfail(dirfd, ch, "cannot open", dirname);

	while((rd = sysgetdents64(dirfd, (struct dirent64*)debuf, delen)) > 0)
	{
		char* ptr = debuf;
		char* end = debuf + rd;

		while(ptr < end)
		{
			struct dirent64* dep = (struct dirent64*) ptr;

			if(!dotddot(dep->d_name))
				recdent(dirname, ch, dep);
			if(!dep->d_reclen)
				break;

			ptr += dep->d_reclen;
		}
	};

	sysclose(dirfd);
};

static void recdent(const char* dirname, struct chmod* ch, struct dirent64* de)
{
	int dirnlen = strlen(dirname);
	int depnlen = strlen(de->d_name);
	char fullname[dirnlen + depnlen + 2];

	char* p = fullname;
	char* e = fullname + sizeof(fullname) - 1;

	p = fmtstr(p, e, dirname);
	p = fmtchar(p, e, '/');
	p = fmtstr(p, e, de->d_name);
	*p++ = '\0';

	struct stat st;
	long ret = syslstat(fullname, &st);

	if(ret < 0)
		mfail(ret, ch, "cannot stat", fullname);
	else
		chmodst(fullname, ch, &st);
}

/* Any relative change (+ or - but not = or octal mode) needs current
   file mode to work with, so we always stat() the file.
   This is in constrast with rm, which only needs de->d_type most of the time.

   No attempts to skip stat() when it's not needed, that's just not worth it. */

static void chmodst(const char* name, struct chmod* ch, struct stat* st)
{
	if((st->st_mode & S_IFMT) == S_IFLNK)
		return;
	if((st->st_mode & S_IFMT) == S_IFDIR && (ch->opts & OPT_r))
		recurse(name, ch);
	else if(ch->opts & OPT_d)
		return;
	if((st->st_mode & S_IFMT) == S_IFDIR && (ch->opts & OPT_n))
		return;

	int mode = st->st_mode;

	mode &= ~ch->clr;
	mode |=  ch->set;

	long ret = syschmod(name, mode);

	if(ret < 0)
		mfail(ret, ch, "cannot chmod", name);
}

static void chmod(const char* name, struct chmod* ch)
{
	struct stat st;
	long ret = sysstat(name, &st);

	if(ret < 0)
		mfail(ret, ch, "cannot stat", name);
	else
		chmodst(name, ch, &st);
}

/* Octal modes are simply raw st_mode values */

static void parseoct(char* modstr, struct chmod* ch)
{
	int n = 0, d;
	char* c;

	for(c = modstr; *c; c++)
		if(*c >= '0' && (d = *c - '0') <= 7)
			n = n * 8 + d;
		else
			fail("bad mode: ", modstr, 0);

	ch->clr = -1;
	ch->set = n;
}

/* For symbolic modes, we do not support multiple clauses like u+w,g-x
   and the X bit which is partially replaced by -d */

static void parsesym(char* cl, struct chmod* ch)
{
	char* p = cl;
	int who = 0;
	int rwx = 0;
	int sid = 0;
	int sticky = 0;
	char op = 0;

	for(; *p; p++) switch(*p) {
		case 'a': who |= 06777; break;
		case 'u': who |= 04700; break;
		case 'g': who |= 02070; break;
		case 'o': who |= 00007; break;
		default: goto op;
	};
	
op:	if(!who)
		who = (0777 & ~sysumask(0));

	switch(*p) {
		case '+':
		case '-':
		case '=': op = *p++; break;
		default: goto done;
	};

	for(; *p; p++) switch(*p) {
		case 'r': rwx |= 04; break;
		case 'w': rwx |= 02; break;
		case 'x': rwx |= 01; break;
		case 's': sid = 3; break;
		case 't': sticky = 1; break;
		default: goto done;
	};

done:	if(*p || !op || (!rwx && !sid && !sticky))
		fail("bad mode specfication:", cl, 0);

	int mode = (rwx | (rwx << 3) | (rwx << 6)) & who;
	if(sid) mode |= 06000 & who;
	if(sticky) mode |= 01000;

	if(op == '-')
		ch->clr = mode;
	else
		ch->set = mode;
	if(op == '=')
		ch->clr = -1;
}

static void parsemod(char* modstr, struct chmod* ch)
{
	if(*modstr >= '0' && *modstr <= '7')
		parseoct(modstr, ch);
	else
		parsesym(modstr, ch);
}

int main(int argc, char** argv)
{
	int i = 1;
	struct chmod ch = { .opts = 0, .set = 0, .clr = 0 };

	if(i < argc && argv[i][0] == '-')
		ch.opts = argbits(OPTS, argv[i++] + 1);

	if(i < argc)
		parsemod(argv[i++], &ch);
	else
		fail("missing arguments", NULL, 0);

	if(i >= argc)
		fail("need file names to work on", NULL, 0);

	while(i < argc)
		chmod(argv[i++], &ch);

	return 0;
}
