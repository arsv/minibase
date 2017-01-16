#include <sys/open.h>
#include <sys/close.h>
#include <sys/stat.h>
#include <sys/getdents.h>

#include <string.h>
#include <format.h>
#include "svcmon.h"

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

static void addfile(char* base, int blen)
{
	struct svcrec* rc;

	if((rc = findrec(base))) {
		rc->flags &= ~P_STALE;
		return;
	}

	if(!(rc = makerec()))
		return report("cannot create svcrec", NULL, 0);

	memset(rc, 0, sizeof(*rc));
	memcpy(rc->name, base, blen);
}

static void tryfile(char* dir, char* base)
{
	int dlen = strlen(dir);
	int blen = strlen(base);
	char path[dlen+blen+2];

	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, base);
	*p++ = '\0';

	struct stat st;

	if(sysstat(path, &st))
		return;
	if((st.st_mode & S_IFMT) != S_IFREG)
		return;
	if(!(st.st_mode & 0111))
		return;
	if(blen > NAMELEN - 1)
		return;

	addfile(base, blen);
}

int load_dir_ents(void)
{
	int delen = PAGE;
	char* debuf = alloc(delen);

	if(!debuf) return -1;

	char* dir = gg.dir;
	long fd, rd;

	if((fd = sysopen(dir, O_RDONLY | O_DIRECTORY)) < 0) {
		report("open", dir, fd);
		afree();
		return fd;
	}

	while((rd = sysgetdents64(fd, debuf, delen)) > 0) {
		char* ptr = debuf;
		char* end = debuf + rd;
		while(ptr < end) {
			struct dirent64* de = (struct dirent64*) ptr;

			if(!de->d_reclen)
				break;

			ptr += de->d_reclen;

			if(dotddot(de->d_name))
				continue;
			if(de->d_type != DT_UNKNOWN && de->d_type != DT_REG)
				continue;

			tryfile(dir, de->d_name);
		}
	} if(rd < 0) {
		report("getdents", dir, rd);
	}

	afree();
	sysclose(fd);
	return rd;
}

static void mark_stale(struct svcrec* rc)
{
	rc->flags |= P_STALE;
}

static void unmark_stale(struct svcrec* rc)
{
	rc->flags &= ~P_STALE;
}

static void disable_stale(struct svcrec* rc)
{
	if(!(rc->flags & P_STALE))
		return;
	if(rc->pid <= 0)
		droprec(rc);
	else
		rc->flags |= P_DISABLED;
}

static void foreach_rec(void (*func)(struct svcrec* rc))
{
	struct svcrec* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		func(rc);
}

int reload(void)
{
	foreach_rec(mark_stale);

	int ret  = load_dir_ents();

	if(!ret)
		foreach_rec(disable_stale);
	else
		foreach_rec(unmark_stale);

	return ret;
}
