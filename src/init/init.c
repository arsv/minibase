#include <sys/clock_gettime.h>
#include <sys/unlink.h>
#include <sys/getpid.h>
#include <sys/getuid.h>
#include <sys/close.h>
#include <sys/fork.h>
#include <sys/waitpid.h>
#include <sys/execve.h>
#include <sys/brk.h>

#include <format.h>

#include <util.h>
#include "init.h"

struct init gg;

static int setup(char** envp)
{
	gg.initdir = INITDIR;
	gg.env = envp;
	gg.uid = sysgetuid();
	gg.brk = (char*)sysbrk(NULL);
	gg.ptr = gg.end = gg.brk;

	setinitctl();

	return setsignals();
}

static void reset(void)
{
	gg.state = 0;
	gg.timetowait = -1;
	gg.outfd = STDERR;

	if(gg.end > gg.brk)
		gg.end = gg.ptr = (char*)sysbrk(gg.brk);
}

char* alloc(int len)
{
	char* ptr = gg.ptr;
	char* req = ptr + len;

	if(req <= gg.end)
		goto done;

	gg.end = (char*)sysbrk(req);

	if(req > gg.end) {
		report("out of memory", NULL, 0);
		return NULL;
	}
done:
	gg.ptr += len;
	return ptr;
}

static void advpasstime(int dflt)
{
	struct timespec tp = { 0, 0 };

	if(sysclock_gettime(CLOCK_MONOTONIC, &tp))
		goto fault;

	time_t shifted = tp.tv_sec + BOOTCLOCKOFFSET;

	if(shifted < gg.passtime)
		goto fault;

	gg.passtime = shifted;
	return;

fault:	gg.passtime += dflt;
}

static int forkreboot(void)
{
	int pid = sysfork();

	if(pid == 0) {
		char arg[] = { '-', gg.rbcode, '\0' };
		char* argv[] = { "/sbin/shutdown", arg, NULL };
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
		goto reboot;	/* Initial setup failed badly */
	if(reload())
		goto reboot;

	advpasstime(BOOTCLOCKOFFSET);

	while(!gg.rbcode) {
		reset();
		initpass();
		waitpoll();

		if(gg.state & S_SIGCHLD)
			waitpids();
		if(gg.state & S_CTRLREQ)
			acceptctl();
		if(gg.state & S_REOPEN)
			setinitctl();
		if(gg.state & S_RELOAD)
			reload();

		advpasstime(gg.timetowait);
	}

	gg.ctlfd = -1;
	
	while(anyrunning()) {
		reset();
		killpass();
		waitpoll();

		if(gg.state & S_SIGCHLD)
			waitpids();

		advpasstime(gg.timetowait);
	}

reboot:
	if(sysgetpid() != 1) {       /* not running as *the* init */
		sysunlink(INITCTL);
		return 0;
	} else {
		return forkreboot();
	}
};
