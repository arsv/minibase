#include <bits/ioctl/socket.h>
#include <sys/creds.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/ioctl.h>

#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>

#include "ifmon.h"
#include "common.h"

int update_link_name(LS)
{
	struct ifreq ifreq;
	int fd = rtnlfd;
	int ret;

	memzero(&ifreq, sizeof(ifreq));

	ifreq.ival = ls->ifi;

	if((ret = sys_ioctl(fd, SIOCGIFNAME, &ifreq)) < 0)
		return ret;

	memcpy(ls->name, ifreq.name, IFNAMESIZ);

	tracef("ifi %i name %s\n", ls->ifi, ifreq.name);

	return 0;
}

void spawn_identify(int ifi, char* name)
{
	int ret, pid;
	char* path = HERE "/etc/net/identify";

	if((ret = sys_access(path, X_OK)) < 0)
		return;

	if((pid = sys_fork()) < 0) {
		warn("fork", name, pid);
		return;
	} else if(pid == 0) {
		char* argv[] = { path, name, NULL };
		int code = sys_execve(*argv, argv, environ);
		if(code) warn("execve", *argv, code);
		_exit(0xff);
	}

	//tracef("spawn(%s %s) = %i\n", path, name, pid);
}

static int spawn(LS, char* path)
{
	int ret, pid;

	FMTBUF(p, e, name, IFNAMESIZ + 2);
	p = fmtstrn(p, e, ls->name, sizeof(ls->name));
	FMTEND(p, e);

	if((ret = sys_access(path, X_OK)) < 0)
		return ret;

	if((pid = sys_fork()) < 0)
		return pid;
	else if(pid == 0) {
		char* argv[] = { path, name, NULL };
		int code = sys_execve(*argv, argv, environ);
		if(code) warn("execve", *argv, code);
		_exit(0xff);
	}

	ls->pid = pid;

	//tracef("spawn(%s %s) = %i\n", path, name, pid);

	return 0;
}

int spawn_mode(LS)
{
	FMTBUF(p, e, script, 100);
	p = fmtstr(p, e, HERE "/etc/net/mode-");
	p = fmtstrn(p, e, ls->mode, sizeof(ls->mode));
	FMTEND(p, e);

	return spawn(ls, script);
}

int spawn_stop(LS)
{
	int ret;

	FMTBUF(p, e, script, 100);
	p = fmtstr(p, e, HERE "/etc/net/stop-");
	p = fmtstrn(p, e, ls->mode, sizeof(ls->mode));
	FMTEND(p, e);

	if((ret = spawn(ls, script)) >= 0)
		return ret;
	if(ret != -ENOENT)
		return ret;

	char* common = HERE "/etc/net/flush";

	return spawn(ls, common);
}

void got_sigchld(void)
{
	struct link* ls;
	int pid, status;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
		//tracef("waitpid yields %i\n", pid);

		for(ls = links; ls < links + nlinks; ls++) {
			if(ls->pid != pid)
				continue;

			ls->pid = 0;
			script_exit(ls, status);

			break;
		}
	}
}
