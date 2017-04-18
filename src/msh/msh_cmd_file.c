#include <bits/ints.h>
#include <sys/open.h>
#include <sys/close.h>
#include <sys/write.h>
#include <sys/dup2.h>

#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

static int openflags(struct sh* ctx, int* dst, char* str)
{
	int flags = 0;
	char* p;

	for(p = str; *p; p++) switch(*p) {
		case 'w': flags |= O_WRONLY; break;
		case 'r': flags |= O_RDONLY; break;
		case 'a': flags |= O_APPEND; break;
		case 'c': flags |= O_CREAT; break;
		case 'x': flags |= O_EXCL; break;
		default: return error(ctx, "open", "unknown flags", 0);
	}

	*dst = flags;
	return 0;
}

int cmd_open(struct sh* ctx, int argc, char** argv)
{
	int fd, rfd, ret, i = 1;
	int flags = O_RDWR;

	if(i < argc && argv[i][0] == '-')
		flags = openflags(ctx, &flags, argv[i++] + 1);
	if((ret = numargs(ctx, argc - i, 2, 2)))
		return ret;

	char* srfd = argv[i++];
	char* name = argv[i++];

	if((ret = argint(ctx, srfd, &rfd)))
		return ret;
	if((fd = sysopen3(name, flags, 0666)) < 0)
		return error(ctx, "open", name, fd);
	if(fd == rfd)
		return 0;

	ret = fchk(sysdup2(fd, rfd), ctx, "dup", srfd);
	sysclose(fd);

	return ret;
}

int cmd_dupfd(struct sh* ctx, int argc, char** argv)
{
	int ret, oldfd, newfd;

	if((ret = numargs(ctx, argc, 3, 3)))
		return ret;
	if((ret = argint(ctx, argv[1], &oldfd)))
		return ret;
	if((ret = argint(ctx, argv[2], &newfd)))
		return ret;

	return fchk(sysdup2(oldfd, newfd), ctx, "dup", argv[1]);
}

int cmd_close(struct sh* ctx, int argc, char** argv)
{
	int ret, fd;

	if((ret = numargs(ctx, argc, 2, 2)))
		return ret;
	if((ret = argint(ctx, argv[1], &fd)))
		return ret;

	return fchk(sysclose(fd), ctx, "close", argv[1]);
}
