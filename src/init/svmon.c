#include <sys/unlink.h>
#include <sys/getpid.h>
#include <sys/getuid.h>
#include <sys/close.h>
#include <sys/fork.h>
#include <sys/waitpid.h>
#include <sys/execve.h>

#include <format.h>
#include <util.h>

#include "svmon.h"

struct svcmon gg;

static int setup(char** envp)
{
	gg.dir = SVDIR;
	gg.env = envp;
	gg.uid = sysgetuid();
	gg.outfd = STDERR;

	setbrk();
	setctl();

	return setsignals();
}

static int forkreboot(void)
{
	int pid = sysfork();

	if(pid == 0) {
		char arg[] = { '-', gg.rbcode, '\0' };
		char* argv[] = { "/sbin/reboot", arg, NULL };
		sysexecve(*argv, argv, gg.env);
	} else if(pid > 0) {
		int status;
		syswaitpid(pid, &status, 0);
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
	if(sysgetpid() != 1) {
		sysunlink(SVCTL);
		return 0;
	} else {
		return forkreboot();
	}
};
