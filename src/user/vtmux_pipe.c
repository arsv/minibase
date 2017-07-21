#include <bits/errno.h>
#include <bits/fcntl.h>
#include <bits/major.h>
#include <sys/sockio.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <format.h>
#include <string.h>
#include <fail.h>

#include "vtmux.h"
#include "vtmux_pipe.h"

static void reply(struct vtx* cvt, int errno)
{
	sys_write(cvt->ctlfd, (void*)&errno, sizeof(errno));
}

static void reply_send_fd(struct vtx* cvt, int errno, int fdts)
{
	sys_write(cvt->ctlfd, (void*)&errno, sizeof(errno));
}

static void send_notification(int tty, int cmd)
{
	struct vtx* cvt;

	for(cvt = consoles; cvt < consoles + nconsoles; cvt++)
		if(cvt->tty == tty)
			break;
	if(!cvt->tty)
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

static struct vtd* grab_dev_slot(void)
{
	int i;

	for(i = 0; i < nvtdevices; i++)
		if(vtdevices[i].fd <= 0)
			break;
	if(i >= INPUTS)
		return NULL;
	if(i == nvtdevices)
		nvtdevices++;

	return &vtdevices[i];
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

static int check_for_duplicate(struct vtd* vd, int dev, int tty)
{
	int i;

	for(i = 0; i < nvtdevices; i++) {
		struct vtd* vx = &vtdevices[i];

		if(vx == vd)
			continue;
		if(vx->tty != tty)
			continue;
		if(vx->dev != dev)
			continue;

		return -ENFILE;
	}

	return 0;
}

static int open_managed_dev(char* path, int mode, int tty)
{
	struct vtd* vd;
	int dfd, ret;

	if(!(vd = grab_dev_slot()))
		return -EMFILE;

	if((dfd = sys_open(path, O_RDWR | O_NOCTTY | O_CLOEXEC)) < 0)
		return dfd;

	if((ret = check_managed_dev(dfd, &vd->dev, tty)) < 0)
		goto close;
	if((ret = check_for_duplicate(vd, vd->dev, tty)) < 0)
		goto close;

	vd->fd = dfd;
	vd->tty = tty;

	if(tty != activetty)
		disable(vd, TEMPORARILY);

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

static void cmd_open(struct vtx* cvt, void* buf, int len)
{
	struct pmsg_open* msg = buf;
	int ret;

	if(len < sizeof(*msg))
		return reply(cvt, -EINVAL);
	if(!is_zstr(msg->path, len - sizeof(*msg)))
		return reply(cvt, -EINVAL);

	if((ret = open_managed_dev(msg->path, msg->mode, cvt->tty)))
		return reply(cvt, ret);

	return reply_send_fd(cvt, PIPE_REP_OK, ret);
}

static void dispatch(struct vtx* cvt, void* buf, int len)
{
	struct pmsg* msg = buf;

	if(len < sizeof(*msg))
		reply(cvt, -EINVAL);
	else if(msg->code == PIPE_CMD_OPEN)
		cmd_open(cvt, buf, len);
	else
		reply(cvt, -ENOSYS);
}

/* For any sane clients, there should be exactly one pending
   command at a time, because the client is expected to wait
   for reply. But, the protocol does not prevent sending cmds
   in bulk, and dropping the loop here does not save us much. */

void handle_pipe(struct vtx* cvt)
{
	int rd, fd = cvt->ctlfd;
	char buf[100];
	int maxlen = sizeof(buf);

	while((rd = sys_recv(fd, buf, maxlen, MSG_DONTWAIT)) > 0) {
		dispatch(cvt, buf, rd);
	} if(rd < 0 && rd != -EAGAIN) {
		warn("recv", NULL, rd);
	}
}
