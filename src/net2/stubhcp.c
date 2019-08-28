#include <sys/signal.h>
#include <sys/sched.h>

#include <main.h>
#include <util.h>

/* Stub implementations of the dhcp client.
   Doesn't actually do anything with the interface, just prints some tracing
   info to stderr. */

ERRTAG("stubhcp");

static void sighandler(int sig)
{
	if(sig == SIGTERM)
		fail(NULL, "SIGTERM", 0);
	if(sig == SIGINT)
		fail(NULL, "SIGINT", 0);
	if(sig == SIGHUP)
		fail(NULL, "SIGHUP", 0);

	fail("signal", NULL, sig);
}

int main(int argc, char** argv)
{
	SIGHANDLER(sa, sighandler, 0);
	int ret;

	if(argc < 2)
		fail("too few arguments", NULL, 0);
	if(argc > 2)
		fail("too many arguments", NULL, 0);

	if((ret = sys_sigaction(SIGTERM, &sa, NULL)) < 0)
		fail("sigaction", "SIGTERM", ret);
	if((ret = sys_sigaction(SIGINT, &sa, NULL)) < 0)
		fail("sigaction", "SIGINT", ret);
	if((ret = sys_sigaction(SIGHUP, &sa, NULL)) < 0)
		fail("sigaction", "SIGHUP", ret);

	warn("configuring", argv[1], 0);

	sys_pause();

	return 0;
}
