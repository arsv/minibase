#include <sys/file.h>
#include <sys/sched.h>
#include <sys/signal.h>

#include <format.h>
#include <util.h>
#include <main.h>

/* Test serer, to be used as a stub for service scripts in ./rc
   Trapping signals (normally sent by init) makes it clear
   what's going on, specifically when and why the process dies. */

ERRTAG("noisy");

void sighandler(int sig)
{
	switch(sig) {
		case SIGKILL: fail("dying on", "SIGKILL", 0);
		case SIGINT:  fail("dying on", "SIGINT", 0);
		case SIGTERM: fail("dying on", "SIGTERM", 0);
		case SIGHUP:  warn("got", "SIGHUP", 0); break;
		case SIGCONT: warn("got", "SIGCONT",0); break;
		default: warn("ignoring unexpected signal", NULL, 0);
	}
}

void trapsig(void)
{
	SIGHANDLER(sa, sighandler, SA_RESTART);

	sys_sigaction(SIGINT,  &sa, NULL);
	sys_sigaction(SIGTERM, &sa, NULL);
	sys_sigaction(SIGHUP,  &sa, NULL);
	sys_sigaction(SIGKILL, &sa, NULL);
	sys_sigaction(SIGCONT, &sa, NULL);
}

void sleepx(int ms)
{
	struct timespec tr;
	struct timespec ts = {
		.sec = ms/1000,
		.nsec = (ms%1000) * 1000000
	};

	while(1) {
		if(!sys_nanosleep(&ts, &tr))
			break;
		if(tr.sec <= 0)
			break;
		ts = tr;
	};
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	int i = 1;

	trapsig();

	while(1) {
		sleepx(1000);
		warn("still alive", NULL, i);

		if(i < 9) i++; else i = 1;
	}
}
