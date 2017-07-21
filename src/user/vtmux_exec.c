#include <bits/ioctl/tty.h>
#include <bits/socket/unix.h>
#include <bits/fcntl.h>

#include <sys/file.h>
#include <sys/kill.h>
#include <sys/fork.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
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

struct vtx* grab_console_slot(void)
{
	int i;

	/* never grab greeter slot this way */
	for(i = 1; i < nconsoles; i++)
		if(consoles[i].pin)
			continue;
		else if(consoles[i].pid <= 0)
			break;
	if(i >= CONSOLES)
		return NULL;
	if(i == nconsoles)
		nconsoles++;

	struct vtx* cvt = &consoles[i];

	if(cvt->tty == initialtty)
		return cvt;

	int tty, ttyfd;

	if((tty = query_empty_tty()) < 0)
		return NULL;
	if((ttyfd = open_tty_device(tty)) < 0)
		return NULL;

	cvt->tty = tty;
	cvt->ttyfd = ttyfd;

	return cvt;
}

int set_slot_command(struct vtx* cvt, char* cmd)
{
	int cmdlen = strlen(cmd);

	if(cmdlen > CMDSIZE - 1)
		return -ENAMETOOLONG;

	memcpy(cvt->cmd, cmd, cmdlen);
	cvt->cmd[cmdlen] = '\0';

	return 0;
}

void free_console_slot(struct vtx* cvt)
{
	memset(cvt->cmd, 0, sizeof(cvt->cmd));

	cvt->pid = 0;
	cvt->ctlfd = 0;

	if(cvt->tty == initialtty)
		return;

	sys_close(cvt->ttyfd);
	cvt->ttyfd = -1;
	cvt->tty = 0;
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

	sys_dup2(ttyfd, 0);
	sys_dup2(ttyfd, 1);
	sys_dup2(ttyfd, 2);
	sys_dup2(ctlfd, 3);

	xchk(sys_execve(*argv, argv, environ), "exec", cmd);

	return 0;
}

static int start_cmd_on(struct vtx* cvt)
{
	int sk[2];
	int ret, pid;

	int domain = AF_UNIX;
	int type = SOCK_SEQPACKET | SOCK_CLOEXEC;
	int proto = 0;

	if((ret = sys_socketpair(domain, type, proto, sk)) < 0)
		return ret;

	if((pid = sys_fork()) < 0)
		return pid;

	if(pid == 0)
		_exit(child_proc(cvt->ttyfd, sk[1], cvt->cmd));

	sys_close(sk[1]);
	cvt->ctlfd = sk[0];
	cvt->pid = pid;

	return 0;
}

int spawn(char* cmd)
{
	int old = activetty;
	struct vtx* cvt;
	int ret = -EAGAIN;

	if(!(cvt = grab_console_slot()))
		return -EMFILE;

	if((ret = set_slot_command(cvt, cmd)))
		goto fail;

	if((ret = activate(cvt->tty)) < 0)
		goto fail;

	if(!(ret = start_cmd_on(cvt)))
		goto done;

	activate(old);
fail:
	free_console_slot(cvt);
done:
	return ret;
}

static struct vtx* find_vt_rec(int tty)
{
	int i;

	for(i = 0; i < nconsoles; i++)
		if(consoles[i].tty == tty)
			return &consoles[i];

	return NULL;
}

int invoke(struct vtx* cvt)
{
	long ret;

	if((ret = activate(cvt->tty)) < 0)
		return ret;

	if(cvt->pid > 0)
		return sys_kill(cvt->pid, SIGCONT);
	else if(cvt->pin)
		return start_cmd_on(cvt);

	return -ENOENT;
}

int switchto(int tty)
{
	struct vtx* cvt = find_vt_rec(tty);

	if(!cvt)
		return activate(tty);
	else
		return invoke(cvt);
}

/* Initial VTs setup: greeter and pinned commands */

static void preset(struct vtx* cvt, char* cmd, int tty)
{
	long ret;

	if((ret = set_slot_command(cvt, cmd)))
		warn(NULL, cmd, ret);
	else
		cvt->pin = 1;

	if(tty <= 0) return;

	int fd = open_tty_device(tty);

	if(fd < 0) return;

	cvt->tty = tty;
	cvt->ttyfd = fd;
	cvt->ctlfd = -1;
}

static int choose_some_high_tty(int mask)
{
	int i;

	for(i = 10; i < 15; i++)
		if(!(mask & (1<<i)))
			return i;

	return 0;
}

void setup_pinned(char* greeter, int n, char** cmds, int spareinitial)
{
	int mask = 0;
	int i;

	if(n >= CONSOLES - 1)
		fail("too many pre-set commands", NULL, 0);
	if((activetty = lock_switch(&mask)) <= 0)
		fail("cannot setup initial console", NULL, activetty);

	initialtty = activetty;

	for(i = 0; i < n; i++)
		if(i == 0 && !spareinitial)
			preset(&consoles[i+1], cmds[i], initialtty);
		else
			preset(&consoles[i+1], cmds[i], query_empty_tty());

	if(!n && !spareinitial)
		preset(&consoles[0], greeter, initialtty);
	else
		preset(&consoles[0], greeter, choose_some_high_tty(mask));

	nconsoles = n + 1;
}
