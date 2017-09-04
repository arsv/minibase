#include <sys/file.h>
#include <sys/dents.h>

#include <string.h>
#include <format.h>

#include "super.h"

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

	if(sys_stat(path, &st))
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
	char* dir = confdir;
	char* debuf;
	int delen = PAGE;
	long fd, rd = -ENOMEM;

	if((fd = sys_open(dir, O_RDONLY | O_DIRECTORY)) < 0) {
		report("open", dir, fd);
		return fd;
	}

	if(!(debuf = heap_alloc(delen)))
		goto out;

	while((rd = sys_getdents(fd, debuf, delen)) > 0) {
		char* ptr = debuf;
		char* end = debuf + rd;
		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;

			if(!de->reclen)
				break;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;
			if(de->type != DT_UNKNOWN && de->type != DT_REG)
				continue;

			tryfile(dir, de->name);
		}
	} if(rd < 0) {
		report("getdents", dir, rd);
	}

	trim_heap(debuf);
out:
	sys_close(fd);

	return rd;
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
