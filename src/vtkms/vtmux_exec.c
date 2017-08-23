#include <bits/ioctl/vt.h>
#include <bits/ioctl/tty.h>
#include <bits/socket/unix.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/creds.h>

#include <string.h>
#include <format.h>
#include <exit.h>
#include <fail.h>

#include "vtmux.h"
#include "common.h"

int pinmask;

/* Open a new VT and start a client there.

   A client is always a script in /etc/vtx; vtmux starts it with
   stdin/out/err redirected to a newly allocated tty, and provides
   a control socket on fd 3. Everything else, including setuid calls
   and setting VT mode (text or graphic) should be done in the scripts.

   The idea behind these scripts is to limit greeter's ability to run
   arbitrary commands. Also, in a way, they represent the name => cmd
   mapping between via first and the last fields in traditional
   /etc/passwd format.

   Greeter itself is just another pinned client named "LOGIN". */

int open_tty_device(int tty)
{
	char namebuf[30];

	char* p = namebuf;
	char* e = namebuf + sizeof(namebuf) - 1;

	p = fmtstr(p, e, "/dev/tty");
	p = fmtint(p, e, tty);
	*p++ = '\0';

	int fd = sys_open(namebuf, O_RDWR | O_CLOEXEC);

	if(fd < 0)
		warn("open", namebuf, fd);

	return fd;
}

/* All child_* functions run in the child process. */

static void child_prep_fds(int ttyfd, int ctlfd)
{
	sys_dup2(ttyfd, 0);
	sys_dup2(ttyfd, 1);
	sys_dup2(ttyfd, 2);
	sys_dup2(ctlfd, 3);
}

static void child_set_ctty(void)
{
	int ret;

	if((ret = sys_setsid()) < 0)
		warn("setsid", NULL, ret);
	if((ret = sys_ioctl(STDOUT, TIOCSCTTY, 0)) < 0)
		warn("ioctl", "TIOCSCTTY", ret);
}

static int child_proc(int ttyfd, int ctlfd, char* path)
{
	char* argv[] = { path, NULL };

	child_prep_fds(ttyfd, ctlfd);
	child_set_ctty();

	xchk(sys_execve(*argv, argv, environ), "exec", path);

	return 0;
}

static int set_process_mode(int tty, int ttyfd)
{
	struct vt_mode vtm = {
		.mode = VT_PROCESS,
		.waitv = 0,
		.relsig = SIGUSR1,
		.acqsig = SIGUSR2
	};

	return ioctl(ttyfd, VT_SETMODE, &vtm, "VT_SETMODE PROCESS");
}

/* This runs with $tty already active. */

static int start_cmd_on(struct term* cvt, int tty, char* path)
{
	int ttyfd, sk[2];
	int ret, pid;

	int domain = AF_UNIX;
	int type = SOCK_SEQPACKET | SOCK_CLOEXEC;
	int proto = 0;

	if((ttyfd = open_tty_device(tty)) < 0)
		return ttyfd;
	if((ret = set_process_mode(tty, ttyfd)) < 0)
		goto out1;
	if((ret = sys_socketpair(domain, type, proto, sk)) < 0)
		goto out1;

	if((pid = ret = sys_fork()) < 0)
		goto out2;
	if(pid == 0)
		_exit(child_proc(ttyfd, sk[1], path));

	sys_close(sk[1]);

	cvt->ctlfd = sk[0];
	cvt->ttyfd = ttyfd;
	cvt->pid = pid;
	cvt->tty = tty;

	return 0;
out2:
	sys_close(sk[0]);
	sys_close(sk[1]);
out1:
	sys_close(ttyfd);
	return ret;
}

static void make_cmd_path(char* buf, int len, char* dir, char* cmd)
{
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, cmd);
	*p++ = '\0';
}

static int check_cmd_exists(char* path)
{
	return sys_access(path, X_OK);
}

/* Switch to $tty and spawn $cmd there. The code does a quick check
   for $cmd first to avoid rather heavy VT switch operation in case
   there's nothing to run there. The chosen tty must be free. */

int spawn(int tty, char* cmd)
{
	char* dir = CONFDIR;
	char path[strlen(dir) + strlen(cmd) + 5];

	int old = activetty;
	struct term* cvt;
	int ret = -EAGAIN;

	make_cmd_path(path, sizeof(path), dir, cmd);

	if((ret = check_cmd_exists(path)) < 0)
		return ret;
	if(!(cvt = grab_term_slot()))
		return -EMFILE;

	if((ret = activate(tty)) < 0)
		goto fail;
	if(!(ret = start_cmd_on(cvt, tty, path)))
		goto done;

	activate(old);
fail:
	free_term_slot(cvt);
done:
	return ret;
}

int show_greeter(void)
{
	int tty;

	if(greetertty)
		return switchto(greetertty);

	if((tty = query_greeter_tty()) < 0)
		return tty;

	return spawn(tty, "LOGIN");
}

/* The idea behind the following code is to allow the user to dedicate
   certain TTYs to certain clients, so that for instance C-A-F5 would
   always switch to the same user session, spawning it if necessary.

   Pinned clients are defined by their names: /etc/vts/ttyN.

   The set of pinned clients is cached within vtmux. This is to avoid
   checking access() during every VT switch, and also to allow for sane
   empty VT allocation code. The assumption is that the set is mostly
   static while vtmux runs. */

void scan_pinned(void)
{
	int tty;

	char* dir = CONFDIR;
	char buf[strlen(dir) + 20];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/tty");

	pinmask = 0;

	for(tty = 1; tty <= 12; tty++) {
		char* q = fmtint(p, e, tty);
		*q++ = '\0';

		if(sys_access(buf, X_OK) >= 0)
			pinmask |= (1 << tty);
	}
}

int pinned(int tty)
{
	if(tty <= 0 || tty >= 32)
		return 0;

	return (pinmask & (1 << tty));
}

int spawn_pinned(int tty)
{
	char buf[20];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, "tty");
	p = fmtint(p, e, tty);
	*p++ = '\0';

	return spawn(tty, buf);
}
