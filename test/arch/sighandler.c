#include <sys/signal.h>
#include <sys/file.h>
#include <sys/creds.h>
#include <sys/sched.h>

#include <format.h>
#include <util.h>
#include <main.h>

ERRTAG("sighandler");

/* Signal handling, minibase way.
   Should not segfault. See ../../doc/signals.txt on this. */

void sighandler(int sig)
{
	if(sig != SIGCHLD)
		fail("unexpected signal", NULL, sig);
	_exit(0);
}

int main(noargs)
{
	SIGHANDLER(sa, sighandler, 0);
	struct timespec ts = { 1, 0 };
	int ret, self;

	if((self = sys_getpid()) <= 0)
		fail("getpid", NULL, self);
	if((ret = sys_sigaction(SIGCHLD, &sa, NULL)))
		fail("sigaction", NULL, ret);

	sys_kill(self, SIGCHLD);

	if((ret = sys_nanosleep(&ts, NULL)) < 0)
		fail("nanosleep", NULL, ret);

	fail("timeout", NULL, 0);
}
