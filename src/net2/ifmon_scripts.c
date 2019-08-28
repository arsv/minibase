#include <bits/ioctl/socket.h>
#include <sys/creds.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/ioctl.h>

#include <format.h>
#include <string.h>
#include <util.h>

#include "ifmon.h"
#include "common.h"

int update_link_name(CTX, LS)
{
	struct ifreq ifreq;
	int fd = ctx->rtnlfd;
	int ret;

	memzero(&ifreq, sizeof(ifreq));

	ifreq.ival = ls->ifi;

	if((ret = sys_ioctl(fd, SIOCGIFNAME, &ifreq)) < 0)
		return ret;

	memcpy(ls->name, ifreq.name, IFNAMESIZ);

	return 0;
}

int get_running(LS)
{
	return (ls->flags & LS_MASK);
}

void sighup_running_dhcp(LS)
{
	int flags = ls->flags;

	if(!(flags & LF_RUNNING))
		return;
	if((flags & LS_MASK) != LS_DHCP)
		return;

	sys_kill(ls->pid, SIGHUP);
}

void clear_link_mode(CTX, LS)
{
	memzero(ls->mode, sizeof(ls->mode));
	ls->flags &= ~LF_CARRIER;
	ls->pid = 0;
}

static int spawn(CTX, LS, char* path)
{
	int ret, pid;
	char** environ = ctx->environ;

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
	ls->flags |= LF_RUNNING;

	return pid;
}

static void set_current_script(LS, int what)
{
	int flags = ls->flags;

	flags &= ~(LS_MASK | LF_RUNNING);
	flags |= what;

	ls->flags = flags;
}

void spawn_identify(CTX, LS)
{
	char* script = HERE "/etc/net/identify";

	set_current_script(ls, LS_IDEF);

	(void)spawn(ctx, ls, script);
}

void spawn_mode(CTX, LS)
{
	int ret;

	FMTBUF(p, e, script, 100);
	p = fmtstr(p, e, HERE "/etc/net/mode-");
	p = fmtstrn(p, e, ls->mode, sizeof(ls->mode));
	FMTEND(p, e);

	ls->flags &= ~LF_NEED_MODE;

	set_current_script(ls, LS_MODE);

	if((ret = spawn(ctx, ls, script)) > 0)
		return;

	report_mode_errno(ctx, ls, ret);

	ls->flags |= LF_FAILED;
}

void spawn_stop(CTX, LS)
{
	int ret;

	FMTBUF(p, e, script, 100);
	p = fmtstr(p, e, HERE "/etc/net/stop-");
	p = fmtstrn(p, e, ls->mode, sizeof(ls->mode));
	FMTEND(p, e);

	ls->flags &= ~LF_NEED_STOP;

	set_current_script(ls, LS_STOP);

	if((ret = spawn(ctx, ls, script)) > 0)
		return;

	if(ret != -ENOENT) {
		report_stop_errno(ctx, ls, ret);
		return;
	}

	char* common = HERE "/etc/net/flush";

	if((ret = spawn(ctx, ls, common)) > 0)
		return;

	if(ret != -ENOENT) {
		report_stop_errno(ctx, ls, ret);
	} else {
		clear_link_mode(ctx, ls);
		report_stop_exit(ctx, ls, 0);
	}
}

void spawn_dhcp(CTX, LS)
{
	int ret;

	FMTBUF(p, e, script, 100);
	p = fmtstr(p, e, HERE "/etc/net/conf-");
	p = fmtstrn(p, e, ls->mode, sizeof(ls->mode));
	FMTEND(p, e);

	set_current_script(ls, LS_DHCP);

	if((ret = spawn(ctx, ls, script)) > 0)
		return;
	if(ret != -ENOENT)
		return;

	char* common = HERE "/etc/net/config";

	(void)spawn(ctx, ls, common);
}

static void script_exit(CTX, LS, int status)
{
	int what = ls->flags & LS_MASK;

	ls->flags &= ~LF_RUNNING;

	if(what == LS_MODE)
		report_mode_exit(ctx, ls, status);
	else if(what == LS_STOP)
		report_stop_exit(ctx, ls, status);

	if(what == LS_MODE && status)
		ls->flags |= LF_FAILED;

	if(what == LS_STOP)
		clear_link_mode(ctx, ls);
	else
		ls->flags |= LF_MARKED;
}

static struct link* find_link_pid(CTX, int pid)
{
	struct link* links = ctx->links;
	int nlinks = ctx->nlinks;
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		if(!(ls->flags & LF_RUNNING))
			continue;
		else if(ls->pid == pid)
			return ls;

	return NULL;
}

void got_sigchld(CTX)
{
	struct link* ls;
	int pid, status;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
		if(!(ls = find_link_pid(ctx, pid)))
			continue;

		ls->flags &= ~LF_RUNNING;
		ls->pid = status;

		script_exit(ctx, ls, status);
	}
}

static int can_run(LS, int what)
{
	int flags = ls->flags;

	if(flags & LF_RUNNING)
		return 0;
	if(flags & LF_FAILED)
		return 0;
	if(!(flags & what))
		return 0;

	ls->flags = flags & ~what;

	return what;
}

void check_links(CTX)
{
	struct link* links = ctx->links;
	int nlinks = ctx->nlinks;
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!(ls->flags & LF_MARKED))
			continue;

		if(can_run(ls, LF_NEED_MODE))
			spawn_mode(ctx, ls);
		if(can_run(ls, LF_NEED_STOP))
			spawn_stop(ctx, ls);
		if(can_run(ls, LF_NEED_DHCP))
			spawn_dhcp(ctx, ls);

		ls->flags &= ~LF_MARKED;
	}
}
