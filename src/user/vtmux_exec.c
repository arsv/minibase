#include <bits/ioctl/tty.h>
#include <bits/socket/unix.h>
#include <bits/fcntl.h>

#include <sys/open.h>
#include <sys/close.h>
#include <sys/fork.h>
#include <sys/dup2.h>
#include <sys/ioctl.h>
#include <sys/execve.h>
#include <sys/_exit.h>
#include <sys/socketpair.h>

#include <string.h>
#include <format.h>
#include <fail.h>

#include "vtmux.h"

/* Open a new VT and start a client there */

int open_tty_device(int tty)
{
	char namebuf[30];

	char* p = namebuf;
	char* e = namebuf + sizeof(namebuf) - 1;

	p = fmtstr(p, e, "/dev/tty");
	p = fmtint(p, e, tty);
	*p++ = '\0';

	int fd = sysopen(namebuf, O_RDWR | O_CLOEXEC);

	if(fd < 0)
		warn("open", namebuf, fd);

	return fd;
}

struct vtx* grab_console_slot(void)
{
	int i;

	/* never grab greeter slot this way */
	for(i = 1; i < nconsoles; i++)
		if(!consoles[i].tty)
			break;
	if(i >= CONSOLES)
		return NULL;
	if(i == nconsoles)
		nconsoles++;
	
	struct vtx* cvt = &consoles[i];

	int tty;
	long ret = sysioctl(0, VT_OPENQRY, (long)&tty);

	if(ret < 0) {
		warn("ioctl", "VT_OPENQRY", ret);
		return NULL;
	}

	int ttyfd = open_tty_device(tty);

	if(ttyfd < 0)
		return NULL;

	cvt->tty = tty;
	cvt->ttyfd = ttyfd;
	cvt->ctlfd = -1;
	cvt->pid = 0;

	return cvt;
}

static struct vtx* find_any_running(void)
{
	int i;

	for(i = 1; i < nconsoles; i++)
		if(consoles[i].pid > 0)
			return &consoles[i];

	return NULL;
}

static int child_proc(int ttyfd, int ctlfd, char* cmd)
{
	int cmdlen = strlen(cmd);
	char* dir = "/etc/vts";
	int dirlen = strlen(dir);

	char path[dirlen+cmdlen+2];
	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, cmd);
	*p++ = '\0';

	char* argv[] = { path, NULL };

	sysdup2(ttyfd, 0);
	sysdup2(ttyfd, 1);
	sysdup2(ttyfd, 2);
	sysdup2(ctlfd, 3);

	xchk(sysexecve(*argv, argv, environ), "exec", cmd);

	return 0;
}

static int start_cmd_on(struct vtx* cvt, char* cmd)
{
	int sk[2];
	int ret = syssocketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, sk);

	if(ret < 0)
		return ret;

	int pid = sysfork();

	if(pid < 0)
		return pid;

	if(pid == 0)
		_exit(child_proc(cvt->ttyfd, sk[1], cmd));

	sysclose(sk[1]);
	cvt->ctlfd = sk[0];
	cvt->pid = pid;

	return 0;
}

int spawn_client(char* cmd)
{
	int old = activetty;
	struct vtx* cvt = grab_console_slot();

	if(!cvt) return -ENOENT; 

	disengage();
	activate(cvt->tty);

	int ret = start_cmd_on(cvt, cmd);

	if(!ret) return 0; /* success */

	close_dead_vt(cvt);

	activate(old);
	engage(old);

	return ret;
}

void spawn_greeter(void)
{
	struct vtx* cvt = &consoles[0];

	disengage();
	activate(cvt->tty);

	int ret = start_cmd_on(cvt, greeter);

	if(!ret) return; /* success */

	cvt = find_any_running();

	if(!cvt) _exit(0xFF);

	activate(cvt->tty);
	engage(cvt->tty);
}
