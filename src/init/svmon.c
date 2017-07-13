#include <sys/fsnod.h>
#include <sys/pid.h>
#include <sys/creds.h>
#include <sys/file.h>
#include <sys/fork.h>
#include <sys/wait.h>
#include <sys/exec.h>

#include <format.h>
#include <util.h>

#include "svmon.h"

struct top gg;

static int setup(char** envp)
{
	gg.dir = SVDIR;
	gg.env = envp;
	gg.uid = sys_getuid();
	gg.outfd = STDERR;

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

int main(int argc, char** argv, char** envp)
{
	if(setup(envp))
		goto reboot;
	if(reload_procs())
		goto reboot;

	initpass();

	while(!gg.reboot) {
		gg.sigchld = 0;
		gg.reopen = 0;
		gg.reload = 0;
		gg.passreq = 0;

		waitpoll();

		if(gg.sigchld)
			waitpids();
		if(gg.reopen)
			setup_ctrl();
		if(gg.reload)
			reload_procs();
		if(gg.passreq)
			initpass();
		if(gg.heapreq)
			heap_flush();
	}

reboot:
	if(sys_getpid() != 1) {
		sys_unlink(SVCTL);
		return 0;
	} else {
		return spawn_reboot();
	}
};
