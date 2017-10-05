#include <sys/signal.h>
#include <sys/file.h>
#include <sys/creds.h>

#include <format.h>

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

	sys_write(STDOUT, buf, p - buf);
}

int main(void)
{
	int self = sys_getpid();

	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTORER,
		.restorer = sigreturn,
		.mask = EMPTYSIGSET
	};

	sys_sigaction(SIGCHLD, &sa, NULL);
	sys_kill(self, SIGCHLD);

	return 0;
}
