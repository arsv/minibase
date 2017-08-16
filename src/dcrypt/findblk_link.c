#include <sys/file.h>
#include <sys/fsnod.h>
#include <sys/symlink.h>

#include <format.h>

#include "config.h"
#include "findblk.h"

static void link_part(char* name, char* label)
{
	FMTBUF(lp, le, link, 100);
	lp = fmtstr(lp, le, MAPDIR);
	lp = fmtstr(lp, le, "/");
	lp = fmtstr(lp, le, label);
	FMTEND(lp);

	FMTBUF(pp, pe, path, 100);
	pp = fmtstr(pp, pe, "/dev/");
	pp = fmtstr(pp, pe, name);
	FMTEND(pp);

	sys_symlink(path, link);
}

void link_parts(void)
{
	struct part* pt;

	sys_mkdir(MAPDIR, 0755);

	for(pt = parts; pt < parts + nparts; pt++)
		link_part(pt->name, pt->label);
}
