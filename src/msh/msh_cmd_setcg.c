#include <sys/getpid.h>
#include <sys/open.h>
#include <sys/write.h>
#include <sys/close.h>

#include <string.h>
#include <format.h>

#include "msh.h"
#include "msh_cmd.h"

/* This should be dropped as soon as $$ gets implemented.
   Then it would be just "write $$ /sys/fs/cgroup/.../tasks". */

static int setcg(struct sh* ctx, char* base, char* grp, char* pbuf, int plen)
{
	int blen = strlen(base);
	int glen = strlen(grp);
	char path[blen+glen+20];
	int fd, wr;

	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtstr(p, e, base);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, grp);
	p = fmtstr(p, e, "/tasks");
	*p = '\0';

	if((fd = sysopen(path, O_WRONLY)) < 0)
		return fd;

	wr = syswrite(fd, pbuf, plen);

	sysclose(fd);

	return (wr == plen);
}

int cmd_setcg(struct sh* ctx)
{
	char* base = "/sys/fs/cgroup";
	char* group;

	int pid = sysgetpid();
	char pbuf[20];
	char* p = fmtint(pbuf, pbuf + sizeof(pbuf) - 1, pid); *p = '\0';
	int plen = p - pbuf;

	if(noneleft(ctx))
		return -1;
	while((group = shift(ctx)))
		if(setcg(ctx, base, group, pbuf, plen))
			return -1;

	return 0;
}
