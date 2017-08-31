#include <bits/ioctl/loop.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <format.h>
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

static int set_loop_fd(int pfd, int idx)
{
	int lfd, ret;

	if((lfd = open_loop_dev(idx)) < 0)
		return lfd;

	ret = sys_ioctli(lfd, LOOP_SET_FD, pfd);

	sys_close(lfd);

	return ret;
}

/* LOOP_CTL_GET_FREE is somewhat racy since it does not claim the device
   immediately. The chances of failure are pretty low, but still several
   attempts are performed to actually claim the device. LOOP_SET_FD should
   only succeed once per device unless LOOP_CLR_FD gets called in-between. */

int setup_loopback(int pfd)
{
	int ret, cfd;
	char* name = "/dev/loop-control";

	if((cfd = sys_open(name, O_RDONLY)) < 0)
		return cfd;

	for(int i = 0; i < 3; i++) {	
		if((ret = sys_ioctli(cfd, LOOP_CTL_GET_FREE, 0)) < 0)
			break;
		if((ret = set_loop_fd(pfd, ret)) >= 0)
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
