#include <sys/file.h>
#include <sys/dents.h>

#include <string.h>
#include <format.h>

#include "common.h"
#include "svhub.h"

static void addfile(char* base, int blen)
{
	struct proc* rc;

	if((rc = find_by_name(base))) {
		rc->flags &= ~P_STALE;
		return;
	}

	if(!(rc = grab_proc_slot()))
		return report("cannot create proc", NULL, 0);

	memset(rc, 0, sizeof(*rc));
	memcpy(rc->name, base, blen);
}

static void tryfile(int at, char* base)
{
	int blen = strlen(base);

	struct stat st;

	if(sys_fstatat(at, base, &st, 0))
		return;
	if((st.mode & S_IFMT) != S_IFREG)
		return;
	if(!(st.mode & 0111))
		return;
	if(blen > NAMELEN - 1)
		return;

	addfile(base, blen);
}

int load_dir_ents(void)
{
	char* dir = INITDIR;
	int fd, ret;
	char buf[2048];

	if((fd = sys_open(dir, O_RDONLY | O_DIRECTORY)) < 0) {
		report("open", dir, fd);
		return fd;
	}

	while((ret = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		char* ptr = buf;
		char* end = buf + ret;

		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;

			if(!de->reclen)
				break;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;
			switch(de->type) {
				case DT_UNKNOWN:
				case DT_REG:
				case DT_LNK: break;
				default: continue;
			}

			tryfile(fd, de->name);
		}
	} if(ret < 0) {
		report("getdents", dir, ret);
	}

	sys_close(fd);

	return ret;
}

static void mark_stale(struct proc* rc)
{
	rc->flags |= P_STALE;
}

static void unmark_stale(struct proc* rc)
{
	rc->flags &= ~P_STALE;
}

static void disable_stale(struct proc* rc)
{
	if(!(rc->flags & P_STALE))
		return;
	if(rc->pid <= 0)
		free_proc_slot(rc);
	else
		rc->flags |= P_DISABLED;
}

static void foreach_rec(void (*func)(struct proc* rc))
{
	struct proc* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		func(rc);
}

int reload_procs(void)
{
	foreach_rec(mark_stale);

	int ret = load_dir_ents();

	if(ret >= 0)
		foreach_rec(disable_stale);
	else
		foreach_rec(unmark_stale);

	return ret;
}
