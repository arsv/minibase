#include <sys/open.h>
#include <sys/close.h>
#include <sys/write.h>
#include <sys/mkdir.h>
#include <sys/unlink.h>
#include <sys/dup2.h>

#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

static int openflags(struct sh* ctx, char* str, int* dst)
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

int cmd_open(struct sh* ctx)
{
	int fd, rfd, ret;
	int flags = O_RDWR;
	char* name;

	if(noneleft(ctx))
		return -1;
	if(dasharg(ctx))
		if(openflags(ctx, shift(ctx)+1, &flags))
			return -1;
	if(shift_int(ctx, &rfd))
		return -1;
	if(shift_str(ctx, &name))
		return -1;
	if(moreleft(ctx))
		return -1;

	if((fd = sysopen3(name, flags, 0666)) < 0)
		return error(ctx, "open", name, fd);
	if(fd == rfd)
		return 0;

	ret = fchk(sysdup2(fd, rfd), ctx, name);
	sysclose(fd);

	return ret;
}

int cmd_dupfd(struct sh* ctx)
{
	int oldfd, newfd;

	if(shift_int(ctx, &oldfd))
		return -1;
	if(shift_int(ctx, &newfd))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sysdup2(oldfd, newfd), ctx, NULL);
}

int cmd_close(struct sh* ctx)
{
	int fd;

	if(shift_int(ctx, &fd))
		return -1;
	if(moreleft(ctx))
		return -1;

	return fchk(sysclose(fd), ctx, NULL);
}

int cmd_write(struct sh* ctx)
{
	int fd, ret;
	char* data;
	char* name;

	if(shift_str(ctx, &data))
		return -1;
	if(shift_str(ctx, &name))
		return -1;
	if(moreleft(ctx))
		return -1;

	if((fd = sysopen(name, O_WRONLY)) < 0)
		return error(ctx, "open", name, 0);

	int len = strlen(data);
	data[len] = '\n';

	if((ret = syswrite(fd, data, len+1)) < 0)
		return error(ctx, "write", name, ret);
	else if(ret < len)
		return error(ctx, "incomplete write", NULL, 0);

	data[len] = '\0';

	return 0;
}

int cmd_unlink(struct sh* ctx)
{
	char* name;

	if(noneleft(ctx))
		return -1;
	while((name = shift(ctx)))
		if(fchk(sysunlink(name), ctx, name))
			return -1;

	return 0;

}

static int mkdirs(char* name, int mode)
{
	int namelen = strlen(name);
	char* e = name + namelen;
	char* q;
	int ret;

	if(!namelen)
		return -EINVAL;
	if((ret = sysmkdir(name, mode)) >= 0 || ret == -EEXIST)
		return 0;
	if(ret != -ENOENT)
		goto out;

	q = e - 1;

	while(1) {
		for(; q > name && *q != '/'; q--)
			;
		if(*q != '/')
			goto out;
		*q = '\0';

		if((ret = sysmkdir(name, mode)) >= 0)
			break;
		if(ret != -ENOENT)
			goto out;
	} while(q < e) {
		*q = '/';
		for(; q <= e && *q; q++)
			;
		if(*q) goto out;

		if((ret = sysmkdir(name, mode)) < 0)
			goto out;
	}; ret = 0;
out:
	return ret;
}

int cmd_mkdirs(struct sh* ctx)
{
	int mode = 0755;
	char* name;

	if(shift_str(ctx, &name))
		return -1;
	if(numleft(ctx) && shift_oct(ctx, &mode))
		return -1;

	return fchk(mkdirs(name, mode), ctx, name);
}
