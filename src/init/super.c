#include <sys/creds.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/file.h>
#include <sys/proc.h>

#include <format.h>
#include <string.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "super.h"

const char* rbscript;
char** environ;

static short flagged;

int stop_into(const char* script)
{
	int ret;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, BOOTDIR "/");
	p = fmtstr(p, e, script);
	FMTEND(p, e);

	if((ret = sys_access(path, X_OK)) < 0)
		return ret;

	rbscript = script;

	stop_all_procs();

	return 0;
}

static int exec_next(void)
{
	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, BOOTDIR "/");
	p = fmtstr(p, e, rbscript);
	FMTEND(p, e);

	char* argv[] = { path, NULL };

	int ret = sys_execve(path, argv, environ);

	report("exec", path, ret);

	return -1; /* cause kernel panic */
}

void request(int flags)
{
	flagged |= flags;
}

static int need_to(int flag)
{
	int ret = flagged & flag;

	flagged &= ~flag;

	return ret;
}

int main(int argc, char** argv)
{
	environ = argv + argc + 1;

	if(argc > 1)
		report("ignoring extra arguments", NULL, 0);

	setup_heap();
	setup_ctrl();

	if(setup_signals())
		goto reboot;
	if(reload_procs())
		goto reboot;

	check_procs();
	update_poll_fds();

	while(1) {
		wait_poll();

		if(need_to(F_WAIT_PIDS))
			wait_pids();
		if(need_to(F_CHECK_PROCS))
			check_procs();
		if(need_to(F_SETUP_CTRL))
			setup_ctrl();
		if(need_to(F_RELOAD_PROCS))
			reload_procs();
		if(need_to(F_FLUSH_HEAP))
			flush_heap();
		if(need_to(F_TRIM_RING))
			trim_ring_area();
		if(need_to(F_UPDATE_PFDS))
			update_poll_fds();
		if(need_to(F_EXIT_LOOP))
			break;
	}
reboot:
	sys_unlink(CONTROL);

	return exec_next();
};
