#include <sys/nanosleep.h>
#include <sys/write.h>
#include <sys/sigaction.h>
#include <sys/_exit.h>

#include <format.h>
#include <null.h>

/* Signal trap, to be used as a stub for service scripts in ./rc
   Trapping signals (normally sent by init) makes it clear
   what's going on, specifically when and why the process dies.
 
   Usage: trap [tag] [sleep-interval]

   Tagging output helps a lot when several instances say something
   at the same time. */

const char* tag = "trap";
int interval = 10;
int count = 1000;

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

	syswrite(STDOUT, saybuf, p - saybuf);
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

	syssigaction(SIGINT,  &sa, NULL);
	syssigaction(SIGTERM, &sa, NULL);
	syssigaction(SIGHUP,  &sa, NULL);
	syssigaction(SIGKILL, &sa, NULL);
	syssigaction(SIGCONT, &sa, NULL);
}

void sleepx(int ms)
{
	struct timespec tr;
	struct timespec ts = {
		.tv_sec = ms/1000,
		.tv_nsec = (ms%1000) * 1000000
	};

	while(1) {
		if(!sysnanosleep(&ts, &tr))
			break;
		if(tr.tv_sec <= 0)
			break;
		ts = tr;
	};
}

int main(int argc, char** argv)
{
	int i;

	if(argc > 1)
		tag = argv[1];
	if(argc > 2)
		parseint(argv[2], &interval);
	if(argc > 3)
		parseint(argv[3], &count);

	trapsig();
	say("starting");
	for(i = 0; i < count; i++) {
		sleepx(interval);
		eprintf("%s: report %i\n", tag, i);
	}
	say("normal exit");

	return 0;
}
