#include <sys/creds.h>
#include <sys/fpath.h>
#include <sys/file.h>
#include <sys/proc.h>

#include <format.h>
#include <util.h>

#include "super.h"

char* confdir;
char** environ;
char rbcode;

static short flagged;

static int spawn_reboot(void)
{
	int pid = sys_fork();

	if(pid == 0) {
		char arg[] = { '-', rbcode, '\0' };
		char* argv[] = { "/sbin/reboot", arg, NULL };
		sys_execve(*argv, argv, environ);
	} else if(pid > 0) {
		int status;
		sys_waitpid(pid, &status, 0);
	}

	return -1;
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

int main(int argc, char** argv, char** envp)
{
	environ = envp;
	confdir = CONFDIR;

	setup_heap();
	setup_ctrl();

	if(setup_signals())
		goto reboot;
	if(reload_procs())
		goto reboot;

	check_procs();
	update_poll_fds();

	while(!rbcode) {
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
	}
reboot:
	if(sys_getpid() != 1) {
		sys_unlink(CONTROL);
		return 0;
	} else {
		return spawn_reboot();
	}
};
