#include <bits/ioctl/tty.h>
#include <bits/errno.h>
#include <bits/fcntl.h>
#include <bits/major.h>
#include <bits/cmsg.h>
#include <sys/sockio.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <format.h>
#include <string.h>
#include <fail.h>

#include "vtmux.h"

/* Each client gets a pipe (really a socket pair) to the vtmux instance
   that spawned it. The client uses it request access to managed devices
   (DRM and inputs).

   The following implements weston-launch protocol.

   In this implementation, the pipe is also used to notify the client when
   its VT becomes active or inactive. The client needs some sort of note
   from vtmux to attempt re-opening the devices. Kernel-issued signals
   (see ioctl_console(2), VT_SETMODE, acqsig) are not reliable because
   the signal may arrive before vtmux is done switching, prompting the client
   to do DRM management ioctl on a fd that is not yet a DRM master.

   Note there is no way to close fds atm; inputs are closed and re-opened
   on every switch, and DRMs are only closed when the client exits. */

#define PIPE_CMD_OPEN       0

#define PIPE_REP_OK         0
#define PIPE_REP_ACTIVATE   1
#define PIPE_REP_DEACTIVATE 2

struct pmsg {
	int code;
	char payload[];
};

struct pmsg_open {
	int code;
	int mode;
	char path[];
};

static void reply(struct term* cvt, int errno)
{
	sys_send(cvt->ctlfd, (void*)&errno, sizeof(errno), 0);
}

static void reply_send_fd(struct term* cvt, int errno, int fdts)
{
	int ret;

	struct iovec iov = {
		.base = &errno,
		.len = sizeof(errno)
	};
	struct cmsgfd fdm = {
		.len = sizeof(struct cmsgfd),
		.level = SOL_SOCKET,
		.type = SCM_RIGHTS,
		.fd = fdts
	};
	struct msghdr msg = {
		.iov = &iov,
		.iovlen = 1,
		.control = &fdm,
		.controllen = sizeof(fdm)
	};

	if((ret = sys_sendmsg(cvt->ctlfd, &msg, 0)) < 0)
		warn("sendmsg", NULL, ret);
}

static void send_notification(int tty, int cmd)
{
	struct term* cvt;

	if(!(cvt = find_term_by_tty(tty)))
		return;

	reply(cvt, cmd);
}

void notify_activated(int tty)
{
	send_notification(tty, PIPE_REP_ACTIVATE);
}

void notify_deactivated(int tty)
{
	send_notification(tty, PIPE_REP_DEACTIVATE);
}

static int check_managed_dev(int fd, int* dev, int tty)
{
	struct stat st;
	long ret;

	if((ret = sys_fstat(fd, &st)) < 0)
		return ret;

	int maj = major(st.st_rdev);
	int fmt = st.st_mode & S_IFMT;

	if(fmt != S_IFCHR)
		return -EACCES;
	if(maj != DRI_MAJOR && maj != INPUT_MAJOR)
		return -EACCES;

	*dev = st.st_rdev;

	if(*dev != st.st_rdev)
		return -EINVAL; /* 64-bit rdev, drop it */

	return 0;
}

/* Device id is used as a key for cmd_close, so reject requests
   to open the same device twice. For DRI devices, multiple fds
   would also mess up mastering. Inputs would be ok, but there's
   still no point in opening them more than once. */

static int check_for_duplicate(struct mdev* md, int dev, int tty)
{
	struct mdev* mx;

	for(mx = mdevs; mx < mdevs + nmdevs; mx++) {
		if(mx == md)
			continue;
		if(mx->tty != tty)
			continue;
		if(mx->dev != dev)
			continue;

		return -ENFILE;
	}

	return 0;
}

static void set_tty_graph_mode(struct term* vt)
{
	if(!vt->graph) return;

	ioctli(vt->ttyfd, KDSETMODE, 1, "KDSETMODE GRAPHICS");

	vt->graph = 1;
}

static int open_managed_dev(char* path, int mode, struct term* vt)
{
	int tty = vt->tty;
	struct mdev* md;
	int dfd, ret;

	mode &= (O_RDWR | O_NONBLOCK);

	if(!(md = grab_mdev_slot()))
		return -EMFILE;

	if((dfd = sys_open(path, mode | O_NOCTTY | O_CLOEXEC)) < 0)
		return dfd;

	if((ret = check_managed_dev(dfd, &md->dev, tty)) < 0)
		goto close;
	if((ret = check_for_duplicate(md, md->dev, tty)) < 0)
		goto close;

	md->fd = dfd;
	md->tty = tty;

	if(tty != activetty)
		disable(md, TEMPORARILY);
	if(major(md->dev) == DRI_MAJOR)
		set_tty_graph_mode(vt);

	return dfd;
close:
	sys_close(dfd);

	return ret;
}

static int is_zstr(char* buf, int len)
{
	char* p;

	if(!len) return 0;

	for(p = buf; p < buf + len - 1; p++)
		if(!*p) return 0;
	if(*p) return 0;

	return 1;
}

/* A newly-opened device is always in ready-to-use state.
   Any input is, until REVOKE ioctl. DRI fs becomes master one on open
   (unless there are other masters for this device, but then we can't
   steal it anyway).

   There is however a possibility that the open request comes from
   an inactive tty. If this happens, we must disable inputs *and* DRIs.
   For DRIs, the active tty may not have yet opened this particular device,
   so the background one will become master despite being in background. */

static void req_open(struct term* cvt, void* buf, int len)
{
	struct pmsg_open* msg = buf;
	int ret;

	if(len < sizeof(*msg))
		return reply(cvt, -EINVAL);
	if(!is_zstr(msg->path, len - sizeof(*msg)))
		return reply(cvt, -EINVAL);

	if((ret = open_managed_dev(msg->path, msg->mode, cvt)) < 0)
		return reply(cvt, ret);

	return reply_send_fd(cvt, PIPE_REP_OK, ret);
}

static void dispatch_req(struct term* cvt, void* buf, int len)
{
	struct pmsg* msg = buf;

	if(len < sizeof(*msg))
		return reply(cvt, -EINVAL);

	if(msg->code == PIPE_CMD_OPEN)
		req_open(cvt, buf, len);
	else
		reply(cvt, -ENOSYS);
}

/* For any sane clients, there should be exactly one pending
   command at a time, because the client is expected to wait
   for reply. But, the protocol does not prevent sending cmds
   in bulk, and dropping the loop here does not save us much. */

void handle_pipe(struct term* cvt)
{
	int rd, fd = cvt->ctlfd;
	char buf[100];
	int maxlen = sizeof(buf);

	while((rd = sys_recv(fd, buf, maxlen, MSG_DONTWAIT)) > 0) {
		dispatch_req(cvt, buf, rd);
	} if(rd < 0 && rd != -EAGAIN) {
		warn("recv", NULL, rd);
	}
}
