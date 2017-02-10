#include <bits/ioctl/drm.h>
#include <bits/ioctl/input.h>
#include <bits/ioctl/tty.h>
#include <bits/major.h>

#include <sys/kill.h>
#include <sys/ioctl.h>
#include <sys/close.h>

#include <string.h>
#include <null.h>
#include <fail.h>

#include "vtmux.h"

static long ioctl(int fd, int req, long arg, char* name)
{
	long ret;

	if((ret = sysioctl(fd, req, arg)) < 0)
		warn("ioctl", name, ret);

	return ret;
}

#define IOCTL(ff, rr, aa) \
	ioctl(ff, rr, aa, #rr)

/* Locking VT switch prevents non-priviledged processes from switching
   VTs at will, and disabled in-kernel Ctrl-Alt-Fn handlers so we can
   use our own. However, vtmux can't switch VTs against the lock either.
   The lock must be removed first. The procedure is racy, and may end
   up with something other than the requested VT being active when the
   lock is back in place. */

int lock_switch(int* mask)
{
	struct vt_stat vst;
	long ret;

	if((ret = IOCTL(0, VT_LOCKSWITCH, 0)) < 0)
		return ret;

	if((ret = IOCTL(0, VT_GETSTATE, (long)&vst)) < 0)
		return ret;

	if(mask)
		*mask = vst.state;

	return vst.active;
}

int unlock_switch(void)
{
	return IOCTL(0, VT_UNLOCKSWITCH, 0);
}

/* Per current systemd-induced design, DRI devices can be suspended
   and resumed but inputs are irrevocably disabled. There's no point
   in retaining dead fds, clients are aware of that and will re-open
   them anyway.

   It's also a good idea to disable devices before releasing them
   from under a dead client. Leaked fds may still linger about. */

void disable(struct vtd* md, int drop)
{
	int dev = md->dev;
	int maj = major(dev);
	int fd = md->fd;

	if(maj == DRI_MAJOR)
		IOCTL(fd, DRM_IOCTL_DROP_MASTER, 0);
	else if(maj == INPUT_MAJOR)
		IOCTL(fd, EVIOCREVOKE, 0);

	if(!drop && maj != INPUT_MAJOR)
		return;

	sysclose(md->fd);

	md->fd = 0;
	md->dev = 0;
	md->tty = 0;
}

/* Empty VTs are closed/released/disallocated, except for the one
   that the greeter runs on. That one is kept reserved, even if
   the greeter is not running. */

void closevt(struct vtx* cvt, int keepvt)
{
	int i;
	int tty = cvt->tty;

	cvt->pid = 0;

	for(i = 0; i < nvtdevices; i++)
		if(vtdevices[i].tty == tty)
			disable(&vtdevices[i], 1);

	if(cvt->ctlfd > 0) {
		sysclose(cvt->ctlfd);
		cvt->ctlfd = 0;
	}

	if(keepvt) {
		IOCTL(cvt->ttyfd, KDSETMODE, 0);
	} else {
		if(!cvt->pin)
			memset(cvt->cmd, 0, sizeof(cvt->cmd));
		if(!cvt->pin && cvt->tty != initialtty) {
			sysclose(cvt->ttyfd);
			cvt->ttyfd = -1;
			cvt->tty = 0;
			IOCTL(0, VT_DISALLOCATE, cvt->tty);
		}
	}

	pollready = 0;
}

/* Session switch sequence:

        * disengage DRIs and inputs of the current session
        * perform VT switch
        * engage DRIs and inputs of another session

   There are slight variations to this sequence when a session
   is being created or destroyed, those are handled separately
   but with calls to the common code here.

   Regardless of which VT is actually active (see lock_switch
   comments above) fds are fully controlled by vtmux, so activetty
   here is always the one that has them. */

static void disengage(int tty)
{
	int i;

	for(i = 0; i < nvtdevices; i++) {
		struct vtd* mdi = &vtdevices[i];

		if(mdi->tty != tty)
			continue;
		if(mdi->fd <= 0)
			continue;

		disable(mdi, 0);
	}
}

/* Only need to activate DRIs here. It's up to the client to re-open inputs.
   (at least for now until EVIOCREVOKE gets un-revoke support in the kernel) */

static void engage(int tty)
{
	int i;

	for(i = 0; i < nvtdevices; i++) {
		struct vtd* mdi = &vtdevices[i];

		if(mdi->tty != tty)
			continue;
		if(mdi->fd <= 0)
			continue;

		int maj = major(mdi->dev);

		if(maj != DRI_MAJOR)
			continue;

		IOCTL(mdi->fd, DRM_IOCTL_SET_MASTER, 0);
	}
}

/* Order is somewhat important here: we should better disconnect
   all inputs/drm handles from avt before issuing VT_ACTIVATE.
   This means two passes over vtdevices[].

   It is not clear however how much a single pass would hurt.
   Presumable nothing important should happen until the process
   being activated gets SIGCONT, by which time all fds will be
   where they should be anyway.

   VT_WAITACTIVE active below *is* necessary; switch does not
   occur otherwise. XXX: check what's really going on there. */

int activate(int tty)
{
	long ret;
	int tries = 0;

	if(activetty == tty)
		return 0;

	disengage(activetty);

	do {
		if((ret = unlock_switch()) < 0)
			goto out;

		IOCTL(0, VT_ACTIVATE, tty);
		IOCTL(0, VT_WAITACTIVE, tty);

		if((ret = lock_switch(NULL)) < 0)
			goto out;

	} while(ret != tty && tries++ < 5);

	activetty = ret;
	engage(activetty);

	if(activetty != tty)
		ret = -EAGAIN; /* mis-switch */
out:
	return ret;
}
