#include <sys/creds.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/fpath.h>
#include <sys/fprop.h>

#include <format.h>
#include <string.h>
#include <util.h>

#include "ifmon.h"
#include "common.h"

void stop_link_procs(struct link* ls, int drop)
{
	struct proc* ch;
	int ifi = ls->ifi;

	for(ch = procs; ch < procs + nprocs; ch++) {
		if(ch->ifi != ifi)
			continue;
		if(ch->pid <= 0)
			continue;

		sys_kill(ch->pid, SIGTERM);

		if(!drop) continue;

		ch->ifi = 0;
	}
}

int any_pids_left(void)
{
	struct proc* ch;

	for(ch = procs; ch < procs + nprocs; ch++)
		if(ch->pid > 0)
			return 1;

	return 0;
}

void kill_all_procs(struct link* ls)
{
	struct proc* ch;

	for(ch = procs; ch < procs + nprocs; ch++) {
		if(ls && ls->ifi != ch->ifi)
			continue;
		if(ch->pid <= 0)
			continue;

		sys_kill(ch->pid, SIGTERM);
	}
}

int any_procs_left(void)
{
	struct proc* ch;

	for(ch = procs; ch < procs + nprocs; ch++)
		if(ch->pid > 0)
			return 1;

	return 0;
}

int any_procs_running(LS)
{
	struct proc* ch;

	for(ch = procs; ch < procs + nprocs; ch++)
		if(ls->ifi == ch->ifi)
			return 1;

	return 0;
}

static int spawn(int ifi, char* script, char* name)
{
	struct proc* pc;
	int ret, pid;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, ETCNET "/");
	p = fmtstr(p, e, script);
	FMTEND(p, e);

	if((ret = sys_access(path, X_OK)) < 0)
		return ret;
	if(!(pc = grab_proc_slot())) {
		warn("out of slots for", name, 0);
		return -ENOMEM;
	}

	if((pid = sys_fork()) < 0)
		return pid;
	else if(pid == 0) {
		char* argv[] = { path, name, NULL };
		int code = sys_execve(*argv, argv, environ);
		if(code) warn("execve", *argv, code);
		_exit(0xff);
	}

	pc->ifi = ifi;
	pc->pid = pid;

	return 0;
}

void spawn_identify(int ifi, char* name)
{
	spawn(ifi, "identify", name);
}

static int spawn_renew(LS)
{
	return spawn(ls->ifi, "conf-renew", ls->name);
}

static int spawn_setup(LS)
{
	int ifi = ls->ifi;
	char* name = ls->name;
	int ret;

	if(!ls->mode[0])
		return -ENOENT;

	FMTBUF(p, e, script, 100);
	p = fmtstr(p, e, "mode-");
	p = fmtstr(p, e, ls->mode);
	FMTEND(p, e);

	if(!(ret = spawn(ifi, script, name))) {
		ls->flags |= LF_SETUP;
	} else if(ret != -ENOENT) { /* setup script failed at exec stage */
		ls->flags |= LF_ERROR;
		ls->needs = 0;
	}

	return ret;
}

static int spawn_request(LS)
{
	int ret;

	if(!(ret = spawn(ls->ifi, "conf-request", ls->name)))
		ls->flags |= LF_REQUEST;

	return ret;
}

static int spawn_cancel(LS)
{
	ls->flags &= ~LF_DISCONT;

	return spawn(ls->ifi, "conf-cancel", ls->name);
}

static int link_needs(LS, int what)
{
	if(ls->needs & what)
		ls->needs &= ~what;
	else
		what = 0;

	return what;
}

int assess_link(LS)
{
	int busy = -EBUSY;

	if(ls->flags & LF_MISNAMED)
		return busy;
	if(any_procs_running(ls))
		return busy;

	if(link_needs(ls, LN_SETUP))
		if(!spawn_setup(ls))
			return busy;
	if(link_needs(ls, LN_CANCEL))
		if(!spawn_cancel(ls))
			return busy;
	if(link_needs(ls, LN_REQUEST))
		if(!spawn_request(ls))
			return busy;
	if(link_needs(ls, LN_RENEW))
		if(!spawn_renew(ls))
			return busy;

	return 0;
}

void reassess_link(LS)
{
	if(assess_link(ls))
		return;

	report_done(ls);
}

static void script_exit(LS, int status)
{
	int flags = ls->flags;

	ls->flags &= ~(LF_SETUP | LF_REQUEST);

	if(status && (flags & LF_SETUP)) {
		/* setup script failed */
		ls->flags |= LF_ERROR;
		ls->needs = 0;
	} else if((flags & LF_REQUEST) && !status) {
		/* dhcp request script exited */
		ls->flags |= LF_DISCONT;
	}

	reassess_link(ls);
}

void got_sigchld(void)
{
	struct proc* ch;
	struct link* ls;
	int pid, status;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
		if(!(ch = find_proc_slot(pid)))
			continue;

		int ifi = ch->ifi;

		free_proc_slot(ch);

		if(!(ls = find_link_slot(ifi)))
			continue;

		script_exit(ls, status);
	}
}
