#include <sys/file.h>
#include <sys/dents.h>
#include <sys/ppoll.h>
#include <sys/socket.h>

#include <format.h>
#include <string.h>
#include <cmsg.h>
#include <util.h>
#include <main.h>

#define WESTON_LAUNCHER_FD 3

#define WESTON_LAUNCHER_SUCCESS    0
#define WESTON_LAUNCHER_ACTIVATE   1
#define WESTON_LAUNCHER_DEACTIVATE 2

ERRTAG("stubvt");

static struct inp {
	int id;
	int fd;
} inputs[30];

static uint counter;
static uint ninputs;
static uint paused;

static int recv_notification(void)
{
	char buf[16];
	int rd, fd = WESTON_LAUNCHER_FD;

	if((rd = sys_recv(fd, buf, sizeof(buf), 0)) < 0)
		fail("recv", NULL, rd);
	if(rd < 4)
		fail("packet too short", NULL, 0);

	return *((int*)buf);
}

static int recv_fd_reply(void)
{
	int rd, fd = WESTON_LAUNCHER_FD;
	char buf[32];
	char ctl[32];

	struct iovec iov = {
		.base = buf,
		.len = sizeof(buf)
	};
	struct msghdr msg = {
		.iov = &iov,
		.iovlen = 1,
		.control = ctl,
		.controllen = sizeof(ctl)
	};
again:
	if((rd = sys_recvmsg(fd, &msg, 0)) < 0) {
		warn("recv", NULL, rd);
		return -EIO;
	}

	int rep = *((int*)buf);

	if(rep < 0)
		return rep;
	if(rep > 0) {
		paused = rep;
		goto again;
	}

	struct cmsg* cm;
	char* p = msg.control;
	char* e = p + msg.controllen;

	if(!(cm = cmsg_get(p, e, SOL_SOCKET, SCM_RIGHTS))) {
		warn("no fd in reply", NULL, 0);
		return -EINVAL;
	} if(cmsg_paylen(cm) != sizeof(long)) {
		warn("invalid fd size", NULL, cmsg_paylen(cm));
		return -EINVAL;
	}

	return *((int*)cmsg_payload(cm));
}

static int open_restricted(const char* path, int mode)
{
	int ret, fd = WESTON_LAUNCHER_FD;
	int plen = strlen(path) + 1;
	int len = 8 + plen;
	int cmd = 0;
	char buf[len];

	memcpy(buf + 0, &cmd, sizeof(cmd));
	memcpy(buf + 4, &mode, sizeof(mode));
	memcpy(buf + 8, path, plen);

	if((ret = sys_send(fd, buf, len, 0)) < 0) {
		warn("send", NULL, ret);
		return -EIO;
	}

	return recv_fd_reply();
}

static void open_evdev(char* dir, char* name, int id)
{
	int fd;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	if(ninputs >= ARRAY_SIZE(inputs))
		return warn("ignoring", path, 0);

	if((fd = open_restricted(path, O_RDONLY)) < 0)
		return warn(NULL, path, fd);

	struct inp* in = &inputs[ninputs++];

	in->fd = fd;
	in->id = id;

	warn("OK", path, 0);
}

static void open_inputs(void)
{
	char buf[1024];
	char* dir = "/dev/input";
	int fd, rd, id;
	char* p;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		return warn(NULL, dir, fd);

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;
			ptr += de->reclen;

			if(paused)
				goto out;
			if(!de->reclen)
				break;
			if(strncmp(de->name, "event", 5))
				continue;
			if(!(p = parseint(de->name + 5, &id)) || *p)
				continue;

			open_evdev(dir, de->name, id);
		}
	}
out:
	sys_close(fd);
}

#define ESC "\x1B"
#define CSI "\x1B["

static const char reset[] =
	CSI "H"		/* move cursor to origin (1,1) */
	CSI "J"		/* erase display, from cursor */
	CSI "0m"	/* reset attributes */
	ESC "c"		/* reset */
	CSI "?25h"	/* make cursor visible */
	ESC "(B";	/* select G0 charset */

static void announce(void)
{
	FMTBUF(p, e, buf, 50);

	p = fmtraw(p, e, reset, sizeof(reset));

	if(counter++) {
		p = fmtstr(p, e, "Re-activation ");
		p = fmtint(p, e, counter);
	} else {
		p = fmtstr(p, e, "Initial activation");
	}

	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static void deactivate(void)
{
	if(paused == WESTON_LAUNCHER_DEACTIVATE)
		return warn("duplicate DEACTIVATE", NULL, 0);

	paused = WESTON_LAUNCHER_DEACTIVATE;

	for(uint i = 0; i < ninputs; i++) {
		struct inp* in = &inputs[i];

		if(in->fd <= 0)
			continue;

		sys_close(in->fd);

		in->fd = 0;
		in->id = 0;
	}
}

static void activate(void)
{
	if(paused != WESTON_LAUNCHER_DEACTIVATE)
		fail("unexpected ACTIVATE notification", NULL, 0);

	paused = 0;

	announce();
	open_inputs();

	if(paused) warn("activation aborted", NULL, 0);
}

int main(noargs)
{
	announce();
	open_inputs();

	while(1) {
		int cmd = recv_notification();

		if(cmd == WESTON_LAUNCHER_ACTIVATE)
			activate();
		else if(cmd == WESTON_LAUNCHER_DEACTIVATE)
			deactivate();
		else
			fail("unexpected command", NULL, cmd);
	}
}
