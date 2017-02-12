#include <bits/input.h>
#include <bits/major.h>

#include <sys/open.h>
#include <sys/read.h>
#include <sys/close.h>
#include <sys/fstat.h>
#include <sys/ioctl.h>
#include <sys/_exit.h>
#include <sys/getdents.h>
#include <sys/inotify.h>

#include <string.h>
#include <format.h>
#include <fail.h>

#include "vtmux.h"

/* We expect C-A-Fn keyevents from any available keyboards capable
   of generating them. The problem is that the keyboards are just
   indiscriminated eventN entries in /dev/input, so we should identify
   those we need and ignore everything else there. And like everything
   else, keyboards may be hot-pluggable, so we watch the directory for
   changes.

   This should probably not be done like this, it's up to udev
   to classify devices, but we can't rely on udev actually being
   configured to do that yet.

   This file only handles the directory reading/watching part.
   Keymasks and keyboard identification depend on the keycodes we use,
   not on the directory stuff, so all that is in _keys. */

static const char devinput[] = "/dev/input";

static int already_polling(int dev)
{
	int i;

	for(i = 0; i < nkeyboards; i++)
		if(keyboards[i].fd <= 0)
			continue;
		else if(keyboards[i].dev == dev)
			return 1;

	return 0;
}

static int check_event_dev(int fd, int* dev)
{
	struct stat st;

	if(sysfstat(fd, &st) < 0)
		return 0;
	if((st.st_mode & S_IFMT) != S_IFCHR)
		return 0;
	if(major(st.st_rdev) != INPUT_MAJOR)
		return 0;
	if(already_polling(st.st_rdev))
		return 0;
	if((*dev = st.st_rdev) != st.st_rdev)
		return 0;
	if(!prep_event_dev(fd))
		return 0;

	return 1;
}

static struct kbd* grab_keyboard_slot(void)
{
	int i;

	for(i = 0; i < nkeyboards; i++)
		if(keyboards[i].fd <= 0)
			break;
	if(i >= KEYBOARDS)
		return NULL;
	if(i == nkeyboards)
		nkeyboards++;

	return &keyboards[i];
}

static void check_dir_ent(char* dir, char* name)
{
	struct kbd* kb;
	int fd, dev;

	int dirlen = strlen(dir);
	int namelen = strlen(name);
	char path[dirlen + namelen + 2];

	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	*p++ = '\0';

	if((fd = sysopen(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
		return;

	if(!check_event_dev(fd, &dev))
		goto close;
	if(!(kb = grab_keyboard_slot()))
		goto close;

	kb->fd = fd;
	kb->dev = dev;
	kb->mod = 0;
	return;
close:
	sysclose(fd);
}

static int dotddot(char* p)
{
	if(!p[0])
		return 1;
	if(p[0] == '.' && !p[1])
		return 1;
	if(p[1] == '.' && !p[2])
		return 1;
	return 0;
}

static void setwatch(char* dir)
{
	long fd, wd;

	if((fd = sys_inotify_init()) < 0)
		fail("inotify_init", NULL, fd);

	if((wd = sys_inotify_add_watch(fd, dir, IN_CREATE)) < 0)
		fail("inotify_add_watch", dir, wd);

	inotifyfd = fd;
}

void setup_keyboards(void)
{
	char debuf[1024];
	char* dir = (char*)devinput;
	long fd, rd;

	if((fd = sysopen(dir, O_RDONLY | O_DIRECTORY)) < 0)
		fail("cannot open", dir, fd);

	setwatch(dir);

	while((rd = sysgetdents64(fd, debuf, sizeof(debuf))) > 0) {
		char* ptr = debuf;
		char* end = debuf + rd;
		while(ptr < end) {
			struct dirent64* de = (struct dirent64*) ptr;
			ptr += de->d_reclen;

			if(dotddot(de->d_name))
				continue;
			if(!de->d_reclen)
				break;
			if(de->d_type != DT_UNKNOWN && de->d_type != DT_CHR)
				continue;

			check_dir_ent(dir, de->d_name);
		}
	}

	sysclose(fd);
	pollready = 0;
}

void handleino(int fd)
{
	char buf[512];
	int len = sizeof(buf);
	char* dir = (char*)devinput;
	long rd;

	while((rd = sysread(fd, buf, len)) > 0) {
		char* end = buf + rd;
		char* ptr = buf;

		while(ptr < end) {
			struct inotify_event* ino = (void*) ptr;
			ptr += sizeof(*ino) + ino->len;

			check_dir_ent(dir, ino->name);
		}
	} if(rd < 0) {
		warn("inotify-read", dir, rd);
	}
}
