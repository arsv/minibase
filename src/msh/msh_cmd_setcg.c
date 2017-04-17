#include <sys/getpid.h>
#include <sys/open.h>
#include <sys/write.h>
#include <sys/close.h>

#include <string.h>
#include <format.h>

#include "msh.h"
#include "msh_cmd.h"

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

int cmd_setcg(struct sh* ctx, int argc, char** argv)
{
	char* base = "/sys/fs/cgroup";
	int i, ret;

	int pid = sysgetpid();
	char pbuf[20];
	char* p = fmtint(pbuf, pbuf + sizeof(pbuf) - 1, pid); *p = '\0';
	int plen = p - pbuf;

	if((ret = numargs(ctx, argc, 2, 0)))
		return ret;

	for(i = 1; i < argc; i++)
		if((ret = setcg(ctx, base, argv[i], pbuf, plen)))
			break;

	return ret;
}
