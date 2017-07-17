#include <sys/fsnod.h>
#include <sys/pid.h>
#include <sys/file.h>
#include <sys/fork.h>
#include <sys/wait.h>
#include <sys/exec.h>

#include <format.h>
#include <util.h>

#include "svmon.h"

struct top gg;
static short flagged;

static int setup(char** envp)
{
	gg.dir = SVDIR;
	gg.env = envp;

	setup_heap();
	setup_ctrl();

	return setup_signals();
}

static int spawn_reboot(void)
{
	int pid = sys_fork();

	if(pid == 0) {
		char arg[] = { '-', gg.rbcode, '\0' };
		char* argv[] = { "/sbin/reboot", arg, NULL };
		sys_execve(*argv, argv, gg.env);
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
	if(setup(envp))
		goto reboot;
	if(reload_procs())
		goto reboot;

	flagged = F_CHECK_PROCS | F_UPDATE_PFDS;

	while(!gg.rbcode) {
		if(need_to(F_CHECK_PROCS))
			check_procs();
		if(need_to(F_UPDATE_PFDS))
			update_poll_fds();

		wait_poll();

		if(need_to(F_WAIT_PIDS))
			wait_pids();
		if(need_to(F_SETUP_CTRL))
			setup_ctrl();
		if(need_to(F_RELOAD_PROCS))
			reload_procs();
		if(need_to(F_FLUSH_HEAP))
			flush_heap();
		if(need_to(F_TRIM_RING))
			trim_ring_area();
	}
reboot:
	if(sys_getpid() != 1) {
		sys_unlink(SVCTL);
		return 0;
	} else {
		return spawn_reboot();
	}
};
