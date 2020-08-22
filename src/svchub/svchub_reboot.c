#include <sys/proc.h>
#include <sys/fprop.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "svchub.h"

static void note(const char* msg, char* arg, int err)
{
	FMTBUF(p, e, buf, 100);

	p = fmtstr(p, e, "svchub: ");

	if(msg)
		p = fmtstr(p, e, msg);
	if(msg && arg)
		p = fmtstr(p, e, " ");
	if(arg)
		p = fmtstr(p, e, arg);

	if(err) {
		p = fmtstr(p, e, ": ");
		p = fmtint(p, e, err);
	}

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);
}

static noreturn void exec_into(CTX, const char* script)
{
	FMTBUF(p, e, path, 200);
	p = fmtstr(p, e, BOOTDIR);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, script);
	FMTEND(p, e);

	char* argv[] = { path, NULL };

	int ret = sys_execve(path, argv, ctx->environ);

	note(NULL, path, ret);

	_exit(0xFF);
}

void terminate(CTX)
{
	char* script = ctx->rbscript;

	if(!script) script = "failure";

	exec_into(ctx, script);
}

static void stop_all_procs(CTX)
{
	struct proc* rc = procs;
	struct proc* re = procs + ctx->nprocs;
	int ret, count = 0;

	for(; rc < re; rc++) {
		if(empty(rc))
			continue;
		if((ret = stop_proc(ctx, rc)) < 0)
			continue;

		count++;
	}

	if(!count) terminate(ctx);
}

int stop_into(CTX, const char* script)
{
	int ret;

	FMTBUF(p, e, path, 200);
	p = fmtstr(p, e, BOOTDIR);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, script);
	FMTEND(p, e);

	if((ret = sys_access(path, X_OK)) < 0)
		return ret;

	ctx->rbscript = (char*)script;

	stop_all_procs(ctx);

	return 0;
}

static void dump_waiting(CTX)
{
	struct proc* rc = procs;
	struct proc* re = procs + ctx->nprocs;
	char buf[200];

	char* p = buf;
	char* z = buf + sizeof(buf) - 1;
	char* e = z - 20;

	p = fmtstr(p, e, "Still running:");

	for(; rc < re; rc++) {
		if(rc->pid <= 0)
			continue;

		if(p >= e) {
			p = fmtstr(p, z, " ...");
			break;
		} else {
			p = fmtchar(p, e, ' ');
			p = fmtstrn(p, e, rc->name, sizeof(rc->name));
		}
	}

	*p++ = '\n';

	writeall(STDERR, buf, p - buf);
}

void signal_stop(CTX, const char* script)
{
	int sigcnt = ctx->sigcnt;

	if(sigcnt > 1)
		terminate(ctx);
	if(!sigcnt && ctx->rbscript)
		sigcnt = 1;
	if(sigcnt > 0)
		dump_waiting(ctx);
	else
		stop_into(ctx, script);

	ctx->sigcnt++;
}

noreturn void quit(CTX, const char* msg, char* arg, int err)
{
	note(msg, arg, err);

	exec_into(ctx, "failure");
}

