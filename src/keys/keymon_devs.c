#include <bits/input.h>
#include <bits/major.h>

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

   This should probably not be done like this, it's up to udev
   to classify devices, but we can't rely on udev actually being
   configured to do that yet.

   This file only handles the directory scanning and watching part.
   Event masks get queried and set in keymon_mask.c */

static const char devinput[] = "/dev/input";

int inotifyfd;
struct device devices[NDEVICES];
int ndevices;

static int already_polling(int minor)
{
	int i;

	for(i = 0; i < ndevices; i++)
		if(devices[i].fd <= 0)
			continue;
		else if(devices[i].minor == minor)
			return 1;

	return 0;
}

static int check_event_dev(int fd, uint64_t* dev)
{
	struct stat st;

	if(sys_fstat(fd, &st) < 0)
		return 0;
	if((st.mode & S_IFMT) != S_IFCHR)
		return 0;
	if(major(st.rdev) != INPUT_MAJOR)
		return 0;
	if(already_polling(st.rdev))
		return 0;
	if((*dev = st.rdev) != st.rdev)
		return 0;
	if(!try_event_dev(fd))
		return 0;

	return 1;
}

static struct device* grab_device_slot(void)
{
	int i;

	for(i = 0; i < ndevices; i++)
		if(devices[i].fd <= 0)
			break;
	if(i >= NDEVICES)
		return NULL;
	if(i == ndevices)
		ndevices++;

	return &devices[i];
}

static void check_dir_ent(char* dir, char* name)
{
	struct device* kb;
	int fd;
	uint64_t rdev;

	int dirlen = strlen(dir);
	int namelen = strlen(name);

	FMTBUF(p, e, path, dirlen + namelen + 2);
	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
		return;

	if(!check_event_dev(fd, &rdev))
		goto close;
	if(!(kb = grab_device_slot()))
		goto close;

	kb->fd = fd;
	kb->minor = rdev;

	pollready = 0;

	return;
close:
	sys_close(fd);
}

static void setwatch(char* dir)
{
	long fd, wd;

	if((fd = sys_inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) < 0)
		fail("inotify_init", NULL, fd);

	if((wd = sys_inotify_add_watch(fd, dir, IN_CREATE)) < 0)
		fail("inotify_add_watch", dir, wd);

	inotifyfd = fd;
}

void setup_devices(void)
{
	char debuf[1024];
	char* dir = (char*)devinput;
	long fd, rd;

	if((fd = sys_open(dir, O_RDONLY | O_DIRECTORY)) < 0)
		fail("cannot open", dir, fd);

	setwatch(dir);

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

			check_dir_ent(dir, de->name);
		}
	}

	sys_close(fd);
}

void handle_inotify(int fd)
{
	char buf[512];
	int len = sizeof(buf);
	char* dir = (char*)devinput;
	long rd;

	while((rd = sys_read(fd, buf, len)) > 0) {
		char* end = buf + rd;
		char* ptr = buf;

		while(ptr < end) {
			struct inotify_event* ino = (void*) ptr;
			ptr += sizeof(*ino) + ino->len;

			check_dir_ent(dir, ino->name);
		}
	} if(rd < 0 && rd != -EAGAIN) {
		warn("inotify-read", dir, rd);
	}
}
