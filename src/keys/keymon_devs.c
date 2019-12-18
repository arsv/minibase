#include <bits/input.h>
#include <bits/major.h>

#include <sys/ppoll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/dents.h>
#include <sys/inotify.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "keymon.h"

/* We expect C-A-Fn keyevents from any available keyboards capable
   of generating them. The problem is that the keyboards are just
   indiscriminated eventN entries in /dev/input, so we should identify
   those we need and ignore everything else there. And like everything
   else, keyboards may be hot-pluggable, so we watch the directory for
   changes.

   This file only handles the directory scanning and watching part.
   Event masks get queried and set in keymon_mask.c */

static const char devinput[] = "/dev/input";

static int check_event_dev(CTX, int fd)
{
	struct stat st;

	if(sys_fstat(fd, &st) < 0)
		return 0;
	if((st.mode & S_IFMT) != S_IFCHR)
		return 0;
	if(major(st.rdev) != INPUT_MAJOR)
		return 0;
	if(!try_event_dev(ctx, fd))
		return 0;

	return 1;
}

static void check_dir_ent(CTX, int at, char* name)
{
	int fd, idx;
	int flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC;

	if((idx = find_device_slot(ctx)) < 0)
		return;
	if((fd = sys_openat(at, name, flags)) < 0)
		return;

	if(check_event_dev(ctx, fd)) {
		ctx->bits[idx] = 0;
		set_device_fd(ctx, idx, fd);
	} else {
		sys_close(fd);
	}
}

void scan_devices(CTX)
{
	char* dir = (char*)devinput;
	char debuf[1024];
	int fd, rd;
	int id, wd;

	if((fd = sys_open(dir, O_RDONLY | O_DIRECTORY)) < 0)
		fail("cannot open", dir, fd);

	if((id = sys_inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) < 0)
		fail("inotify_init", dir, id);
	if((wd = sys_inotify_add_watch(id, dir, IN_CREATE)) < 0)
		fail("inotify_add_watch", dir, wd);

	while((rd = sys_getdents(fd, debuf, sizeof(debuf))) > 0) {
		char* ptr = debuf;
		char* end = debuf + rd;

		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;
			ptr += de->reclen;

			if(dotddot(de->name))
				continue;
			if(!de->reclen)
				break;
			if(de->type != DT_UNKNOWN && de->type != DT_CHR)
				continue;

			check_dir_ent(ctx, fd, de->name);
		}
	}

	ctx->dfd = fd;

	set_static_fd(ctx, 1, id);
}

void handle_inotify(CTX, int fd)
{
	char* dir = (char*)devinput;
	char buf[512];
	int rd;

	while((rd = sys_read(fd, buf, sizeof(buf))) > 0) {
		char* end = buf + rd;
		char* ptr = buf;

		while(ptr < end) {
			struct inotify_event* ino = (void*) ptr;

			ptr += sizeof(*ino) + ino->len;

			check_dir_ent(ctx, ctx->dfd, ino->name);
		}
	} if(rd < 0 && rd != -EAGAIN) {
		fail("inotify-read", dir, rd);
	}
}
