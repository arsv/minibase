#include <bits/ioctl/drm.h>
#include <bits/ioctl/input.h>
#include <bits/ioctl/tty.h>
#include <bits/major.h>

#include <sys/file.h>
#include <sys/ioctl.h>

#include <string.h>
#include <null.h>
#include <fail.h>

#include "vtmux.h"

long ioctl(int fd, int req, long arg, const char* name)
{
	long ret;

	if((ret = sys_ioctli(fd, req, arg)) < 0)
		warn("ioctl", name, ret);

	return ret;
}

#define IOCTL(ff, rr, aa) \
	ioctl(ff, rr, aa, #rr)

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

static int looks_active(int tty)
{
	int mask, ret;

	if((ret = query_empty_mask(&mask)) < 0)
		return ret;

	return (mask & (1 << tty));
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

void disable(struct mdev* md, int drop)
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

	sys_close(md->fd);
	free_mdev_slot(md);
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

		IOCTL(md->fd, DRM_IOCTL_SET_MASTER, 0);
	}

	notify_activated(tty);
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

static int switch_wait(int tty)
{
	int ret;

	if((ret = sys_ioctli(0, VT_ACTIVATE, tty)) < 0)
		return ret;
	if((ret = sys_ioctli(0, VT_WAITACTIVE, tty)) < 0)
		return ret;

	return 0;
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
		fail("cannot lock VT switching", NULL, 0);

	activetty = ret;
	initialtty = ret;

	if(!primarytty)
		primarytty = ret;
}

int switchto(int tty)
{
	int ret;

	if(tty < 0)
		return -EINVAL;
	if(tty == 0)
		return show_greeter();

	if(find_term_by_tty(tty))
		return activate(tty);
	if((ret = looks_active(tty)) < 0)
		return ret;
	else if(ret)
		return activate(tty);

	if(pinned(tty))
		return spawn_pinned(tty);

	return -ENOENT;
}

void restore_initial_tty(void)
{
	int tty = initialtty;

	unlock_switch();

	if(initialtty == activetty)
		return;

	switch_wait(tty);
}
