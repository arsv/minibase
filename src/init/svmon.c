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

struct svcmon gg;

static int setup(char** envp)
{
	gg.dir = SVDIR;
	gg.env = envp;
	gg.uid = sys_getuid();
	gg.outfd = STDERR;

	setbrk();
	setctl();

	return setsignals();
}

static int forkreboot(void)
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
	if(reload())
		goto reboot;

	initpass();

	while(!(gg.state & S_REBOOT)) {
		gg.state = 0;

		waitpoll();

		if(gg.state & S_SIGCHLD)
			waitpids();
		if(gg.state & S_REOPEN)
			setctl();
		if(gg.state & S_RELOAD)
			reload();
		if(gg.state & S_PASSREQ)
			initpass();
	}

reboot:
	if(sys_getpid() != 1) {
		sys_unlink(SVCTL);
		return 0;
	} else {
		return forkreboot();
	}
};
