#include <sys/sigaction.h>
#include <sys/kill.h>
#include <sys/write.h>
#include <sys/getpid.h>

#include <format.h>
#include <null.h>

/* Signal handling, minibase way.
   Should not segfault. See ../../doc/signals.txt on this. */

void sighandler(int sig)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, "Signal ");
	p = fmtint(p, e, sig);
	*p++ = '\n';

	syswrite(STDOUT, buf, p - buf);
}

int main(void)
{
	int self = sysgetpid();

	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTORER,
		.restorer = sigreturn,
		.mask = { }
	};

	syssigaction(SIGCHLD, &sa, NULL);
	syskill(self, SIGCHLD);

	return 0;
}
