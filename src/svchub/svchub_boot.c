#include <sys/fprop.h>
#include <sys/proc.h>
#include <sys/timer.h>
#include <sys/signal.h>

#include <sigset.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "svchub.h"

static int spawn_script(CTX, char* name, int dryrun)
{
	char* dir = BOOTDIR "/";
	int dlen = strlen(dir);
	int nlen = strlen(name);
	int len = dlen + nlen + 2;
	char* path = alloca(len);

	char* p = path;
	char* e = path + len - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, name);

	*p++ = '\0';

	char* argv[] = { name, NULL };
	int ret, pid;

	if(ctx->scrpid > 0)
		return -EBUSY;

	if((ret = sys_access(path, X_OK)) < 0)
		return ret;
	else if(dryrun)
		return 0;

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);

	if(pid == 0) {
		ret = sys_execve(path, argv, ctx->envp);
		fail(NULL, name, ret);
	}

	ctx->scrpid = pid;

	return 0;
}

static void spawn_shutdown(CTX)
{
	char* script = ctx->script;

	ctx->state = S_SHUTDOWN;

	close_socket(ctx);

	if(!script)
		script = "failure";
	if(spawn_script(ctx, script, 0) >= 0)
		return;

	fail("no shutdown script", NULL, 0);
}

static void shutdown_exit(CTX, int status)
{
	fail("shutdown script", status ? "failed" : "exited", 0);
}

void start_script(CTX)
{
	int ret;

	if((ret = spawn_script(ctx, "startup", 0)) < 0)
		fail("startup", NULL, ret);

	ctx->state = S_STARTUP;
}

static void startup_done(CTX, int status)
{
	if(status) {
		fail("startup failed", NULL, 0);
	} else {
		ctx->state = S_RUNNING;
	}
}

static void script_exit(CTX, int status)
{
	int state = ctx->state;

	ctx->scrpid = 0;
	ctx->state = 0;

	if(state == S_STARTUP)
		return startup_done(ctx, status);
	if(state == S_SHUTDOWN)
		return shutdown_exit(ctx, status);
}

static void stop_all_procs(CTX)
{
	int i, nprocs = ctx->nprocs;

	for(i = 0; i < nprocs; i++) {
		struct proc* pc = &procs[i];

		int flags = pc->flags;
		int pid = pc->pid;

		if(!flags || !pid)
			continue;
		if(flags & P_STATUS)
			continue;

		if(sys_kill(pid, SIGTERM) < 0)
			continue;

		pc->flags |= P_KILLED;
	}
}

static void arm_shutdown_timer(void)
{
	int ret;

	if((ret = sys_alarm(2)))
		warn("alarm", NULL, ret);
}

static void report_stuck(CTX, struct proc* pc)
{
	int len = strnlen(pc->name, sizeof(pc->name));
	char* name = alloca(len + 1);

	memcpy(name, pc->name, len);
	name[len] = '\0';

	warn("waiting for", name, 0);
}

static void report_hung_procs(CTX)
{
	int i, nprocs = ctx->nprocs;

	for(i = 0; i < nprocs; i++) {
		struct proc* pc = &procs[i];

		int flags = pc->flags;
		int pid = pc->pid;

		if(!flags || !pid)
			continue;
		if(flags & P_STATUS)
			continue;

		report_stuck(ctx, pc);
	}
}

static void force_shutdown(CTX)
{
	int i, nprocs = ctx->nprocs;

	warn("killing remaining services", NULL, 0);

	for(i = 0; i < nprocs; i++) {
		struct proc* pc = &procs[i];

		int flags = pc->flags;
		int pid = pc->pid;

		if(!flags || !pid)
			continue;
		if(flags & P_STATUS)
			continue;

		(void)sys_kill(pid, SIGKILL);

		pc->pid = 0;
	}

	spawn_shutdown(ctx);
}

void handle_alarm(CTX)
{
	int state = ctx->state;

	if(state != S_STOPPING)
		return;

	if(ctx->sigcnt++ < 3) {
		report_hung_procs(ctx);
		arm_shutdown_timer();
	} else {
		ctx->sigcnt = 0;
		force_shutdown(ctx);
	}
}

int command_stop(CTX, char* script)
{
	int ret;

	if(ctx->script)
		return -EALREADY;

	if((ret = spawn_script(ctx, script, 1)) < 0)
		return ret;

	warn("initiating", script, 0);

	ctx->state = S_STOPPING;
	ctx->script = script;
	ctx->sigcnt = 0;

	stop_all_procs(ctx);
	arm_shutdown_timer();

	return 0;
}

void signal_stop(CTX, char* script)
{
	int ret, state = ctx->state;

	if(state == S_STOPPING) {
		force_shutdown(ctx);
		return;
	} else if(state == S_SHUTDOWN) {
		fail("aborting", NULL, 0);
	}

	if((ret = command_stop(ctx, script)) < 0)
		warn(script, "handler", ret);
}

static struct proc* find_by_pid(CTX, int pid)
{
	struct proc* rc = procs;
	struct proc* re = procs + ctx->nprocs;

	if(pid <= 0)
		return NULL;

	for(; rc < re; rc++)
		if(rc->flags & P_STATUS)
			continue;
		else if(rc->pid == pid)
			return rc;

	return NULL;
}

void check_children(CTX)
{
	int pid, status;
	struct proc* rc;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
		if(pid == ctx->scrpid)
			script_exit(ctx, status);
		else if((rc = find_by_pid(ctx, pid)))
			proc_died(ctx, rc, status);
	}

	if(ctx->nalive || ctx->scrpid > 0)
		return;

	if(ctx->state == S_RUNNING)
		warn("all children died", NULL, 0);
	if(ctx->state == S_SHUTDOWN)
		fail("failure at shutdown", NULL, 0);

	spawn_shutdown(ctx);
}
