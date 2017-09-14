#include <bits/ioctl/drm.h>
#include <bits/ioctl/input.h>
#include <bits/ioctl/vt.h>
#include <bits/major.h>

#include <sys/file.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <sys/ioctl.h>

#include <sigset.h>
#include <string.h>
#include <util.h>

#include "vtmux.h"

static int switching;

long ioctl(int fd, int req, void* arg, const char* name)
{
	long ret;

	if((ret = sys_ioctl(fd, req, arg)) < 0)
		warn("ioctl", name, ret);

	return ret;
}

long ioctli(int fd, int req, long arg, const char* name)
{
	return ioctl(fd, req, (void*)arg, name);
}

/* This is incredibly racy, but the only alternative is to hold all
   pinned VTs open so that VT_OPENQRY would skip them. And then again,
   VT_OPENQRY itself is racy. It is probably impossible to do userspace
   VT allocation correctly in Linux.

   Considering typicaly usage, let's just pretend that no other process
   would ever attempt to grab concurrently with vtmux. It's not like
   system-logind is any better. */


static int query_empty_mask(int* mask)
{
	struct vt_stat vst;
	int ret;

	if((ret = sys_ioctl(0, VT_GETSTATE, &vst)) < 0)
		return ret;

	*mask = vst.state;

	return 0;
}

static int usable(int mask, int tty)
{
	if(pinned(tty))
		return 0;

	return !(mask & (1 << tty));
}

int query_empty_tty(void)
{
	int mask, ret, tty;

	if((ret = query_empty_mask(&mask)) < 0)
		return ret;

	for(tty = 1; tty <= 32; tty++)
		if(usable(mask, tty))
			return tty;

	return -ENOTTY;
}

int query_greeter_tty(void)
{
	int mask, ret, tty;

	if((ret = query_empty_mask(&mask)) < 0)
		return ret;

	for(tty = 12; tty >= 10; tty--)
		if(usable(mask, tty))
			return tty;
	for(tty = 13; tty <= 20; tty++)
		if(usable(mask, tty))
			return tty;

	return query_empty_tty();
}

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

	if((ret = ioctl(0, VT_LOCKSWITCH, NULL, "VT_LOCKSWITCH")) < 0)
		return ret;

	if((ret = ioctl(0, VT_GETSTATE, &vst, "VT_GETSTATE")) < 0)
		return ret;

	if(mask)
		*mask = vst.state;

	return vst.active;
}

int unlock_switch(void)
{
	return ioctl(0, VT_UNLOCKSWITCH, NULL, "VT_UNLOCKSWITCH");
}

/* DRI devices can be suspended and resumed but inputs are irrevocably
   disabled. There's no point in retaining dead input fds, clients are
   aware of that and will re-open them anyway.

   It's also a good idea to disable devices before releasing them
   from under a dead client. Leaked fds may still linger about.
   However, some clients do that themselves, so DROP_MASTER may
   routinely return EINVAL. */

static void drop_drm_master(int fd, int final)
{
	int ret;

	if((ret = sys_ioctli(fd, DRM_IOCTL_DROP_MASTER, 0)) >= 0)
		return;
	if(ret == -EINVAL && final)
		return;

	warn("ioctl", "DROP_MASTER", ret);
}

void disable(struct mdev* md, int drop)
{
	int dev = md->dev;
	int maj = major(dev);
	int fd = md->fd;

	if(maj == DRI_MAJOR)
		drop_drm_master(fd, drop);
	else if(maj == INPUT_MAJOR)
		ioctl(fd, EVIOCREVOKE, 0, "EVIOCREVOKE");

	if(drop || maj == INPUT_MAJOR) {
		/* mark the device as gone and request cleanup */
		md->tty = 0;
		md->dev = 0;
		mdevreq = 1;
	}
}

void flush_mdevs(void)
{
	struct mdev* md;
	int n = nmdevs;

	for(md = mdevs; md < mdevs + n; md++)
		if(!md->dev) {
			sys_close(md->fd);
			free_mdev_slot(md);
		}
}

/* Final treatment for FDs of a client that died. Just closing them
   is not a good idea since client might have leaked some to its children. */

void disable_all_devs_for(int tty)
{
	struct mdev* md;

	for(md = mdevs; md < mdevs + nmdevs; md++)
		if(md->tty == tty)
			disable(md, PERMANENTLY);
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
	struct mdev* md;

	for(md = mdevs; md < mdevs + nmdevs; md++) {
		if(md->tty != tty)
			continue;
		if(md->fd <= 0)
			continue;

		disable(md, TEMPORARILY);
	}

	notify_deactivated(tty);
}

/* Only need to activate DRIs here. It's up to the client to re-open inputs.
   (at least for now until EVIOCREVOKE gets un-revoke support in the kernel) */

static void engage(int tty)
{
	struct mdev* md;

	for(md = mdevs; md < mdevs + nmdevs; md++) {
		if(md->tty != tty)
			continue;
		if(md->fd <= 0)
			continue;
		if(major(md->dev) != DRI_MAJOR)
			continue;

		ioctl(md->fd, DRM_IOCTL_SET_MASTER, NULL, "SET_MASTER");
	}

	notify_activated(tty);
}

/* Helpers for the WTF code below */

void switch_sigalrm(void)
{
	switching = -1;
}

void switch_sigusr1(void)
{
	struct term* vt;

	if(switching != 1)
		return;

	switching = 0;

	if(!(vt = find_term_by_tty(activetty)))
		return;

	ioctli(vt->ttyfd, VT_RELDISP, 1, "VT_RELDISP");
}

static void prep_switch_masks(sigset_t* smask, sigset_t* tmask)
{
	sigemptyset(smask);
	sigaddset(smask, SIGUSR1);
	sigaddset(smask, SIGUSR2);

	sigemptyset(tmask);
	sigaddset(smask, SIGUSR1);
	sigaddset(smask, SIGUSR2);
	sigaddset(smask, SIGALRM);
}

static void prep_switch_timer(struct itimerval* itv)
{
	memzero(itv, sizeof(*itv));
	itv->value.sec = 1;
}

/* OMG WTF. The problem here: VT_WAITACTIVE is blocking and non-restartable.
   We need a timeout on top of it, and we should expect SIGUSR as well which
   must be acknowledged. If we run out of time, we declare the switch failed
   and try to lock everything again, almost for sure disabling VT switching
   until the kernel recovers. But at least the current session should remain
   usable.

   Scary cases aside, the most common path here is VT_ACTIVATE followed by
   VT_WAITACTIVE immediately interrupted by SIGUSR1 followed by the second
   VT_WAITACTIVE which succeeds.

   Contrary to what might be expected, SIGUSR1 does not generally arrive
   in-between VT_ACTIVATE and VT_WAITACTIVE, even though the kernel sends
   it while handling VT_ACTIVATE request.

   Logind guys apparently just gave up on VT_WAITACTIVE and decided to poll
   /sys/class/tty/tty0/active instead. */

static int switch_wait(int tty)
{
	int ret;
	sigset_t smask, tmask, origmask;
	struct itimerval old, itv;
	prep_switch_masks(&smask, &tmask);
	prep_switch_timer(&itv);

	sys_setitimer(0, &itv, &old);
	sys_sigprocmask(SIG_UNBLOCK, &smask, &origmask);

	switching = 1;

	if((ret = sys_ioctli(0, VT_ACTIVATE, tty)) < 0)
		goto out;

	while(1) {
		if((ret = sys_ioctli(0, VT_WAITACTIVE, tty)) >= 0)
			break;
		else if(ret != -EINTR)
			break;

		sys_sigprocmask(SIG_BLOCK, &tmask, NULL);

		if(switching == -1) /* timeout */
			break;

		sys_sigprocmask(SIG_UNBLOCK, &tmask, NULL);
	}
out:
	sys_sigprocmask(SIG_SETMASK, &origmask, NULL);
	sys_setitimer(0, &old, NULL);

	if(switching)
		switching = 0;

	return ret;
}

int activate(int tty)
{
	long ret, swret;
	int tries = 0;

	if(activetty == tty)
		return 0;

	disengage(activetty);

	do {
		if((ret = unlock_switch()) < 0)
			return ret;

		swret = switch_wait(tty);

		if((ret = lock_switch(NULL)) < 0)
			return ret;

	} while(ret != tty && !swret && tries++ < 5);

	activetty = ret;
	engage(activetty);

	if(swret < 0)
		return swret;
	else if(activetty != tty)
		return -EAGAIN; /* mis-switch */

	return ret;
}

/* Grab lock on startup, do some switching on C-A-Fn, and try to restore
   the initial state before exiting. */

void grab_initial_lock(void)
{
	int ret;

	if((ret = lock_switch(NULL)) < 0)
		quit("cannot lock VT switching", NULL, 0);

	activetty = ret;
	initialtty = ret;

	if(!primarytty)
		primarytty = ret;
}

int switchto(int tty)
{
	if(tty < 0)
		return -EINVAL;
	if(tty == 0)
		return show_greeter();

	if(find_term_by_tty(tty))
		return activate(tty);

	if(pinned(tty))
		return spawn_pinned(tty);

	return -ENOENT;
}

void restore_initial_tty(void)
{
	unlock_switch();

	if(initialtty == activetty)
		return;

	switch_wait(initialtty);
}
