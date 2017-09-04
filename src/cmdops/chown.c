#include <sys/file.h>
#include <sys/dents.h>
#include <sys/fprop.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <errtag.h>
#include <util.h>

#define OPTS "rfdngu"
#define OPT_r (1<<0)
#define OPT_f (1<<1)
#define OPT_d (1<<2)
#define OPT_n (1<<3)
#define OPT_g (1<<4)
#define OPT_u (1<<5)
/* these are not-option flags in opts */
#define SET_uid (1<<16)
#define SET_gid (1<<17)

#define DEBUFSIZE 2000

/* Something like a half of the code here is copied from chmod.c.
   That's because there shouldn't be chmod or chown (or chgrp),
   it should be a single tool to modify inode props. */

ERRTAG("chown");
ERRLIST(NEACCES NEBUSY NEFAULT NEIO NEISDIR NELOOP NENOENT NENOMEM
	NENOTDIR NEPERM NEROFS NEOVERFLOW NENOSYS NEBADF NEINVAL);

struct chown {
	int uid;
	int gid;
	int opts;
};

static void recdent(const char* dirname, struct chown* ch, struct dirent* de);
static void chownst(const char* entname, struct chown* ch, struct stat* st);

/* With -f we keep going *and* suppress the message.
   (hm, would be better to report it but keep going, maybe?) */

static void mfail(long ret, struct chown* ch, const char* msg, const char* obj)
{
	if(ch->opts & OPT_f)
		return;
	warn(msg, obj, ret);
	_exit(-1);
}

static void recurse(const char* dirname, struct chown* ch)
{
	char debuf[DEBUFSIZE];
	const int delen = sizeof(debuf);
	long rd;

	long dirfd = sys_open(dirname, O_DIRECTORY);

	if(dirfd < 0)
		return mfail(dirfd, ch, "cannot open", dirname);

	while((rd = sys_getdents(dirfd, (struct dirent*)debuf, delen)) > 0)
	{
		char* ptr = debuf;
		char* end = debuf + rd;

		while(ptr < end)
		{
			struct dirent* dep = (struct dirent*) ptr;

			if(!dotddot(dep->name))
				recdent(dirname, ch, dep);
			if(!dep->reclen)
				break;

			ptr += dep->reclen;
		}
	};

	sys_close(dirfd);
};

static void recdent(const char* dirname, struct chown* ch, struct dirent* de)
{
	int dirnlen = strlen(dirname);
	int depnlen = strlen(de->name);
	char fullname[dirnlen + depnlen + 2];

	char* p = fullname;
	char* e = fullname + sizeof(fullname) - 1;

	p = fmtstr(p, e, dirname);
	p = fmtchar(p, e, '/');
	p = fmtstr(p, e, de->name);
	*p++ = '\0';

	struct stat st;
	long ret = sys_lstat(fullname, &st);

	if(ret < 0)
		mfail(ret, ch, "cannot stat", fullname);
	else
		chownst(fullname, ch, &st);
}

static void chownst(const char* name, struct chown* ch, struct stat* st)
{
	if((st->mode & S_IFMT) == S_IFLNK)
		return;
	if((st->mode & S_IFMT) == S_IFDIR && (ch->opts & OPT_r))
		recurse(name, ch);
	else if(ch->opts & OPT_d)
		return;
	if((st->mode & S_IFMT) == S_IFDIR && (ch->opts & OPT_n))
		return;

	int uid = (ch->opts & SET_uid) ? ch->uid : st->uid;
	int gid = (ch->opts & SET_gid) ? ch->gid : st->gid;

	long ret = sys_chown(name, uid, gid);

	if(ret < 0)
		mfail(ret, ch, "cannot chown", name);
}

static void chown(const char* name, struct chown* ch)
{
	struct stat st;
	long ret = sys_stat(name, &st);

	if(ret < 0)
		mfail(ret, ch, "cannot stat", name);
	else
		chownst(name, ch, &st);
}

/* Key argument parsing is the only place where chown differs significantly
   from chown. Like in chmod, raw values are allowed.
   /etc/passwd and /etc/group happen to share the same format for the name => id
   part, so we use a common parser. */

static char* mapfile(const char* name, int* size)
{
	long fd = xchk(sys_open(name, O_RDONLY), "cannot open", name);

	struct stat st;	
	xchk(sys_fstat(fd, &st), "cannot stat", name);	
	/* get larger-than-int files out of the picture */
	if(st.size > 0x7FFFFFFF)
		fail("file too large:", name, 0);


	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	long ret = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if(mmap_error(ret))
		fail("cannot mmap", name, ret);

	*size = st.size;
	return (char*)ret;
}

/* We do not close fd and never unmap the area explicitly.
   It's a shared r/o map, and there's at most two of them
   anyway, so that would be just two pointless syscalls. */

static int mapid(char* name, char* file, char* notfound)
{
	int filesize;
	char* filedata = mapfile(file, &filesize);
	char* fileend = filedata + filesize;
	int id;

	/* user:x:500:...\n */
	/* ls  ue un     le */
	char *ls, *le;
	char *ue, *un;
	char *ne = NULL;
	for(ls = filedata; ls < fileend; ls = le + 1) {
		le = strecbrk(ls, fileend, '\n');
		ue = strecbrk(ls, le, ':');
		if(ue >= le) continue;
		un = strecbrk(ue + 1, le, ':') + 1;
		if(un >= le) continue;

		if(strncmp(name, ls, ue - ls))
			continue;

		ne = parseint(un, &id);
		break;
	};

	if(!ne || *ne != ':')
		fail(notfound, name, 0);
	else
		return id;
}

static int parseid(char* str, char* file, char* notfound)
{
	int id;
	char* p = parseint(str, &id);

	if(p && !*p)
		return id;
	else
		return mapid(str, file, notfound);
}

static void parseown(char* userstr, struct chown* ch)
{
	char* user = NULL;
	char* group = NULL;

	char* sep = strcbrk(userstr, ':');

	if(ch->opts & OPT_g)
		group = userstr;
	else
		user = userstr;

	if(*sep && !(ch->opts & OPT_u)) {
		*sep = '\0';
		group = sep + 1;
	} if(user && *user) {
		ch->uid = parseid(user, "/etc/passwd", "unknown user");
		ch->opts |= SET_uid;
	} if(group && *group) {
		ch->gid = parseid(group, "/etc/group", "unknown group");
		ch->opts |= SET_gid;
	}
}

int main(int argc, char** argv)
{
	int i = 1;
	struct chown ch = { .opts = 0, .uid = 0, .gid = 0 };

	if(i < argc && argv[i][0] == '-')
		ch.opts = argbits(OPTS, argv[i++] + 1);

	if(i < argc)
		parseown(argv[i++], &ch);
	else
		fail("missing arguments", NULL, 0);

	if(i >= argc)
		fail("need file names to work on", NULL, 0);

	while(i < argc)
		chown(argv[i++], &ch);

	return 0;
}
