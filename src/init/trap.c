#include <sys/file.h>
#include <sys/sched.h>
#include <sys/signal.h>

#include <format.h>
#include <exit.h>
#include <null.h>

/* Signal trap, to be used as a stub for service scripts in ./rc
   Trapping signals (normally sent by init) makes it clear
   what's going on, specifically when and why the process dies.
 
   Usage: trap [tag] [sleep-interval]

   Tagging output helps a lot when several instances say something
   at the same time. */

const char* tag = "trap";
int interval = 1000;

#define BUF 1024
static char saybuf[BUF];

void say(const char* what)
{
	char* p = saybuf;
	char* e = saybuf + sizeof(saybuf);

	p = fmtstr(p, e, tag);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, what);
	p = fmtstr(p, e, "\n");

	sys_write(STDOUT, saybuf, p - saybuf);
}

void sighandler(int sig)
{
	switch(sig) {
		case SIGKILL: say("dying on SIGKILL"); _exit(0); break;
		case SIGINT:  say("dying on SIGINT");  _exit(0); break;
		case SIGTERM: say("dying on SIGTERM"); _exit(0); break;
		case SIGHUP:  say("got SIGHUP"); break;
		case SIGCONT: say("got SIGCONT, continuing"); break;
		default: say("ignoring unexpected signal"); break;
	}
}

void trapsig(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTART | SA_RESTORER,
		.restorer = sigreturn
	};

	sys_sigaction(SIGINT,  &sa, NULL);
	sys_sigaction(SIGTERM, &sa, NULL);
	sys_sigaction(SIGHUP,  &sa, NULL);
	sys_sigaction(SIGKILL, &sa, NULL);
	sys_sigaction(SIGCONT, &sa, NULL);
}

void sleepx(int sec)
{
	struct timespec tr;
	struct timespec ts = {
		.sec = sec,
		.nsec = 0
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
	if(argc > 1)
		tag = argv[1];
	if(argc > 2)
		parseint(argv[2], &interval);

	trapsig();
	say("starting");
	sleepx(interval);
	say("normal exit");

	return 0;
}
