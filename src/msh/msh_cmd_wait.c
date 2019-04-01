#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/inotify.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

static int check_file(CTX, char* path)
{
	struct stat st;
	int ret;

	if((ret = sys_stat(path, &st)) >= 0)
		return 0;
	if(ret == -ENOENT)
		return 1;

	return error(ctx, NULL, path, ret);
}

static int match_event(struct inotify_event* ino, char* name)
{
	return !strcmp(name, ino->name);
}

static int watch_ino_for(CTX, int fd, char* name, struct timespec* ts)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int ret, rd;

	while(1) {
		if((ret = sys_ppoll(&pfd, 1, ts, NULL)) < 0)
			return error(ctx, "ppoll", NULL, ret);
		if(ret == 0)
			return -ETIMEDOUT;

		char buf[512];
		int len = sizeof(buf);

		if((rd = sys_read(fd, buf, len)) < 0)
			return error(ctx, "inotify", NULL, rd);

		char* end = buf + rd;
		char* ptr = buf;

		while(ptr < end) {
			struct inotify_event* ino = (void*) ptr;

			if(match_event(ino, name))
				return 0;

			ptr += sizeof(*ino) + ino->len;
		}
	}
}

static void prep_dirname(char* dir, int dlen, char* name, char* base)
{
	if(base > name) {
		int len = base - name - 1;
		if(len > dlen - 1) len = dlen - 1;
		memcpy(dir, name, len);
		dir[len] = '\0';
	} else {
		dir[0] = '.';
		dir[1] = '\0';
	}
}

static int waitfor(CTX, int fd, struct timespec* ts, char* name)
{
	char* base = basename(name);
	char dir[base - name + 3];
	long wd, ret;

	prep_dirname(dir, sizeof(dir), name, base);

	if((wd = sys_inotify_add_watch(fd, dir, IN_CREATE)) >= 0)
		goto got;
	else if(wd != -ENOENT)
		goto err; /* something's wrong with the parent dir */
	else if(!strcmp(dir, "/"))
		goto err; /* no point in waiting for / to appear */

	if((ret = waitfor(ctx, fd, ts, dir)) < 0)
		goto out; /* bad parent dir, or timed out waiting */

	if((wd = sys_inotify_add_watch(fd, dir, IN_CREATE)) >= 0)
		goto got;
err:
	return error(ctx, "inotify", name, wd);
got:
	if((ret = check_file(ctx, name)) <= 0)
		; /* the file already exists */
	else ret = watch_ino_for(ctx, fd, base, ts);

	sys_inotify_rm_watch(fd, wd);
out:
	return ret;
}

int cmd_waitfor(CTX)
{
	char* name;
	int timeout;

	if((shift_str(ctx, &name)))
		return -1;
	if(!numleft(ctx))
		timeout = 2;
	else if(shift_int(ctx, &timeout))
		return -1;

	struct timespec ts = { timeout, 0 };
	int fd, ret;

	if((fd = sys_inotify_init1(IN_NONBLOCK)) < 0)
		return error(ctx, "inotify", NULL, fd);

	ret = waitfor(ctx, fd, &ts, name);

	sys_close(fd);

	if(ret == -ETIMEDOUT)
		return error(ctx, "timeout waiting for", name, 0);

	return ret;
}
