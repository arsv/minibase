#include <bits/ioctl/tty.h>
#include <bits/ioctl/common.h>
#include <bits/socket/unix.h>

#include <sys/file.h>
#include <sys/kill.h>
#include <sys/fork.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/pgrp.h>
#include <sys/exec.h>
#include <sys/socket.h>

#include <string.h>
#include <format.h>
#include <exit.h>
#include <fail.h>

#include "vtmux.h"

/* Open a new VT and start a client there.

   A client is always a script in /etc/vtx; vtmux starts it with
   stdin/out/err redirected to a newly allocated tty, and provides
   a control socket on fd 3. Everything else, including setuid calls
   and setting VT mode (text or graphic) should be done in the scripts.

   The idea behind these scripts is to limit greeter's ability to run
   arbitrary commands. Also, in a way, they represent the name => cmd
   mapping between via first and the last fields in traditional
   /etc/passwd format.

   Greeter itself is just another pinned client named "login". */

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

int query_empty_tty(void)
{
	int tty;
	long ret;

	if((ret = sys_ioctl(0, VT_OPENQRY, &tty)) < 0)
		warn("ioctl", "VT_OPENQRY", ret);

	return tty;
}

struct term* allocate_console(void)
{
	struct term* cvt;
	int tty;

	if(!(cvt = grab_term_slot()))
		return NULL;
	if((tty = query_empty_tty()) < 0)
		return NULL;

	cvt->tty = tty;

	return cvt;
}

int set_slot_command(struct term* cvt, char* cmd)
{
	int cmdlen = strlen(cmd);

	if(cmdlen > CMDSIZE - 1)
		return -ENAMETOOLONG;

	memcpy(cvt->cmd, cmd, cmdlen);
	cvt->cmd[cmdlen] = '\0';

	return 0;
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

	child_prep_fds(ttyfd, ctlfd);
	child_set_ctty();

	xchk(sys_execve(*argv, argv, environ), "exec", cmd);

	return 0;
}

static int start_cmd_on(struct term* cvt)
{
	int ttyfd, sk[2];
	int ret, pid;

	int domain = AF_UNIX;
	int type = SOCK_SEQPACKET | SOCK_CLOEXEC;
	int proto = 0;

	if((ttyfd = open_tty_device(cvt->tty)) < 0)
		return ttyfd;

	if((ret = sys_socketpair(domain, type, proto, sk)) < 0)
		return ret;

	if((pid = sys_fork()) < 0)
		return pid;

	if(pid == 0)
		_exit(child_proc(ttyfd, sk[1], cvt->cmd));

	sys_close(sk[1]);
	sys_close(ttyfd);

	cvt->ctlfd = sk[0];
	cvt->pid = pid;

	return 0;
}

int spawn(char* cmd)
{
	int old = activetty;
	struct term* cvt;
	int ret = -EAGAIN;

	if(!(cvt = allocate_console()))
		return -EMFILE;

	if((ret = set_slot_command(cvt, cmd)))
		goto fail;

	if((ret = activate(cvt->tty)) < 0)
		goto fail;

	if(!(ret = start_cmd_on(cvt)))
		goto done;

	activate(old);
fail:
	free_term_slot(cvt);
done:
	return ret;
}

int invoke(struct term* cvt)
{
	long ret;

	if((ret = activate(cvt->tty)) < 0)
		return ret;

	if(cvt->pid > 0)
		return sys_kill(cvt->pid, SIGCONT);
	else if(cvt->pin)
		return start_cmd_on(cvt);
	else
		return -ENOENT;
}

int switchto(int tty)
{
	struct term* cvt;

	if((cvt = find_term_by_tty(tty)))
		return invoke(cvt);
	else
		return activate(tty);
}

/* Initial VTs setup: greeter and pinned commands */

static void preset(struct term* cvt, char* cmd, int tty)
{
	long ret;

	if(tty <= 0)
		fail("no tty for", cmd, 0);
	if((ret = set_slot_command(cvt, cmd)))
		fail("name too long:", cmd, 0);

	cvt->pin = 1;
	cvt->tty = tty;
	cvt->ctlfd = -1;
}

static int choose_empty_tty(int mask, int last)
{
	int i;

	for(i = last + 1; i < 15; i++)
		if(!(mask & (1<<i)))
			return i;

	return 0;
}

void setup_pinned(char* greeter, int n, char** cmds)
{
	struct term* gvt;
	struct term* cvt;
	int tty, mask, i;

	if((tty = lock_switch(&mask)) <= 0)
		fail("cannot setup initial console", NULL, tty);
	if(!(gvt = grab_term_slot()))
		fail("no slots left for greeter", NULL, 0);

	initialtty = activetty = tty;
	gvt->pin = 1;

	tty = 0;

	for(i = 0; i < n; i++) {
		if(!(cvt = grab_term_slot()))
			fail("too many preset vts", NULL, 0);
		if((tty = choose_empty_tty(mask, tty)) <= 0)
			fail("no VTs left for", cmds[i], 0);

		preset(cvt, cmds[i], tty);
	}

	if(tty < 10)
		tty = 10;
	if((tty = choose_empty_tty(mask, tty)) <= 0)
		fail("no VTs left for greeter", NULL, 0);

	preset(gvt, greeter, tty);
}
