#include <bits/ioctl/drm.h>
#include <bits/ioctl/input.h>
#include <bits/ioctl/tty.h>
#include <bits/major.h>

#include <sys/kill.h>
#include <sys/ioctl.h>
#include <sys/close.h>

#include <null.h>
#include <fail.h>
#include "vtmux.h"

/* None of ioctl should ever fail. If they do, there's little we can do
   other than issuing a warning and proceeding as if nothing happened. */

static void ioctl(int fd, int req, long arg, char* name)
{
	long ret = sysioctl(fd, req, arg);

	if(ret >= 0) return;

	warn("ioctl", name, ret);
}

#define IOCTL(ff, rr, aa) \
	ioctl(ff, rr, aa, #rr)

/* Per current systemd-induced design, dri devices can be suspended
   and resumed but inputs are irrevocably disabled. There's no point
   in retaining dead fds, clients are aware of that and will re-open
   them anyway.
 
   It's also a good idea to disable devices before releasing them from
   under a dead client, which might have leaked fds to its child
   processes. */

static void disable_device(struct vtd* md, int drop)
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

static int client_is_greeter(struct vtx* cvt)
{
	return (cvt == &consoles[0]);
}

void close_dead_vt(struct vtx* cvt)
{
	int i;
	int tty = cvt->tty;

	for(i = 0; i < nvtdevices; i++)
		if(vtdevices[i].tty == tty)
			disable_device(&vtdevices[i], 1);

	if(cvt->ctlfd > 0)
		sysclose(cvt->ctlfd);

	cvt->ctlfd = 0;

	if(!client_is_greeter(cvt)) {
		sysclose(cvt->ttyfd);
		cvt->ttyfd = 0;
		cvt->tty = 0;
	}

	cvt->pid = 0;
}

static struct vtx* find_vt_rec(int tty)
{
	int i;

	for(i = 0; i < nconsoles; i++)
		if(consoles[i].tty == tty)
			return &consoles[i];

	return NULL;
}

static struct vtx* find_pid_rec(int pid)
{
	int i;

	for(i = 0; i < nconsoles; i++)
		if(consoles[i].pid == pid)
			return &consoles[i];

	return NULL;
}

static void send_signal_to_vt_master(int tty, int sig)
{
	struct vtx* cvt = find_vt_rec(tty);

	if(cvt && cvt->pid > 0)
		syskill(cvt->pid, sig);
}

/* Session switch sequence:
   	* disengage DRIs and inputs of the current session
	* perform VT switch
	* engage DRIs and inputs of another session

   There are slight variations to this sequence when a session
   is being created or destroyed, those are handled separately
   but with calls to the common code here. */

void disengage(void)
{
	int i;
	int tty = activetty;

	for(i = 0; i < nvtdevices; i++) {
		struct vtd* mdi = &vtdevices[i];

		if(mdi->tty != tty)
			continue;
		if(mdi->fd <= 0)
			continue;

		disable_device(mdi, 0);
	}

	send_signal_to_vt_master(tty, SIGTSTP);

	activetty = 0;
}

/* Only need to activate DRIs here. It's up to the client to re-open inputs.
   (at least for now until EVIOCREVOKE gets un-revoke support in the kernel) */

void engage(int tty)
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

	send_signal_to_vt_master(tty, SIGCONT);

	activetty = tty;
}

static int anything_running_on(int tty)
{
	struct vtx* cvt = find_vt_rec(tty);

	return (cvt && cvt->pid > 0);
}

void activate(int tty)
{
	IOCTL(0, VT_ACTIVATE, tty);
}

/* Order is somewhat important here: we should better disconnect
   all inputs/drm handles from avt before issuing VT_ACTIVATE.
   This means two passes over vtdevices[].

   It is not clear however how much a single pass would hurt.
   Presumable nothing important should happen until the process
   being activated gets SIGCONT, by which time all fds will be
   where they should be anyway. */

int switchto(int tty)
{
	if(activetty == tty)
		return 0; /* already there */
	if(!anything_running_on(tty))
		return -ENOENT;

	disengage();
	activate(tty);
	engage(tty);

	return 0;
}

static void switch_to_greeter(void)
{
	struct vtx* cvt = &consoles[0];

	if(cvt->pid > 0)
		switchto(cvt->tty);
	else
		spawn_greeter();
}

void close_dead_client(int pid)
{
	struct vtx* cvt = find_pid_rec(pid);

	if(!cvt) return;

	int tty = cvt->tty;

	close_dead_vt(cvt);

	if(tty == activetty)
		switch_to_greeter();

	if(!find_vt_rec(tty))
		IOCTL(0, VT_DISALLOCATE, tty);

	request_fds_update();
}
