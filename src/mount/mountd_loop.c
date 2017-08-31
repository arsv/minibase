#include <bits/ioctl/loop.h>
#include <sys/ioctl.h>
#include <sys/file.h>

#include <format.h>
#include <string.h>
#include "mountd.h"

/* When a file mount is requested, mountd needs to set up a loop
   device and mount it. The setup part returns idx aka N in /dev/loopN. */

static int open_loop_dev(int idx)
{
	FMTBUF(p, e, path, 30);
	p = fmtstr(p, e, "/dev/loop");
	p = fmtint(p, e, idx);
	FMTEND(p, e);

	return sys_open(path, O_RDONLY);
}

static void set_loop_name(struct loop_info64* info, char* base)
{
	int need_len = strlen(base);
	int have_len = sizeof(info->file_name) - 1;

	int len;
	char* buf = (char*)info->file_name;

	if(need_len > have_len)
		len = have_len;
	else
		len = need_len;

	memcpy(buf, base, len);
	buf[len] = '\0';
}

static int set_loop_fd(int pfd, int idx, char* base)
{
	int lfd, ret;

	struct loop_info64 info;
	memzero(&info, sizeof(info));

	if((lfd = open_loop_dev(idx)) < 0)
		return lfd;

	if((ret = sys_ioctli(lfd, LOOP_SET_FD, pfd)) >= 0) {
		set_loop_name(&info, base);
		sys_ioctl(lfd, LOOP_SET_STATUS64, &info);
	}

	sys_close(lfd);

	return ret;
}

/* LOOP_CTL_GET_FREE is somewhat racy since it does not claim the device
   immediately. The chances of failure are pretty low, but still several
   attempts are performed to actually claim the device. LOOP_SET_FD should
   only succeed once per device unless LOOP_CLR_FD gets called in-between. */

int setup_loopback(int pfd, char* base)
{
	int ret, idx = -1, cfd;
	char* control = "/dev/loop-control";

	if((cfd = sys_open(control, O_RDONLY)) < 0)
		return cfd;

	for(int i = 0; i < 3; i++) {	
		if((ret = sys_ioctli(cfd, LOOP_CTL_GET_FREE, 0)) < 0)
			break;
		if(ret == idx)
			break;

		idx = ret;

		if((ret = set_loop_fd(pfd, idx, base)) < 0)
			continue;

		ret = idx;
		break;
	}

	sys_close(cfd);

	return ret;
}

int unset_loopback(int idx)
{
	int fd;

	if((fd = open_loop_dev(idx)) < 0)
		return fd;

	return sys_ioctli(fd, LOOP_CLR_FD, 0);
}
