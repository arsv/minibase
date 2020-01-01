#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/inotify.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

static int nonexistant(CTX, char* path)
{
	struct stat st;
	int ret;

	if((ret = sys_stat(path, &st)) >= 0)
		return 0;
	if(ret != -ENOENT)
		error(ctx, NULL, path, ret);

	return 1;
}

static int match_event(struct inotify_event* ino, char* name)
{
	return !strcmp(name, ino->name);
}

static void watch_ino_for(CTX, int fd, char* name, struct timespec* ts)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int ret, rd;
next:
	if((ret = sys_ppoll(&pfd, 1, ts, NULL)) < 0)
		error(ctx, "ppoll", NULL, ret);
	if(ret == 0)
		error(ctx, NULL, name, -ETIMEDOUT);

	char buf[512];
	int len = sizeof(buf);

	if((rd = sys_read(fd, buf, len)) < 0)
		error(ctx, "inotify", NULL, rd);

	char* end = buf + rd;
	char* ptr = buf;

	while(ptr < end) {
		struct inotify_event* ino = (void*) ptr;

		if(match_event(ino, name))
			return;

		ptr += sizeof(*ino) + ino->len;
	}

	goto next;
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
	long ret;

	prep_dirname(dir, sizeof(dir), name, base);

	if((ret = sys_inotify_add_watch(fd, dir, IN_CREATE)) >= 0)
		goto got;
	else if(ret != -ENOENT)
		goto err; /* something's wrong with the parent dir */
	else if(!strcmp(dir, "/"))
		goto err; /* no point in waiting for / to appear */

	if((ret = waitfor(ctx, fd, ts, dir)) < 0)
		goto out; /* bad parent dir, or timed out waiting */

	if((ret = sys_inotify_add_watch(fd, dir, IN_CREATE)) >= 0)
		goto got;
got:
	if(nonexistant(ctx, name))
		watch_ino_for(ctx, fd, base, ts);

	sys_inotify_rm_watch(fd, ret);
out:
	return ret;
err:
	error(ctx, "inotify", name, ret);
}

void cmd_waitfor(CTX)
{
	char* name = shift(ctx);
	int timeout;

	if(got_more_arguments(ctx))
		shift_int(ctx, &timeout);
	else
		timeout = 2;

	no_more_arguments(ctx);

	struct timespec ts = { timeout, 0 };
	int fd, ret;

	if((fd = sys_inotify_init1(IN_NONBLOCK)) < 0)
		error(ctx, "inotify", NULL, fd);

	ret = waitfor(ctx, fd, &ts, name);

	sys_close(fd);

	check(ctx, "wait", name, ret);
}
