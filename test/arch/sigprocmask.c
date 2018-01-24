#include <sys/creds.h>
#include <sys/sched.h>
#include <sys/signal.h>

#include <errtag.h>
#include <string.h>
#include <sigset.h>
#include <util.h>

ERRTAG("sigprocmask");

int main(void)
{
	sigset_t mask, old;
	struct timespec ts = { 0, 200*1000*1000 };
	int pid = sys_getpid();
	int ret;

	/* Check if sigprocmask accepts SIG* constants */

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	if((ret = sys_sigprocmask(SIG_BLOCK, &mask, &old)))
		fail("sigprocmask", "SIG_BLOCK", ret);
	if((ret = sys_sigprocmask(SIG_UNBLOCK, &mask, NULL)))
		fail("sigprocmask", "SIG_UNBLOCK", ret);
	if((ret = sys_sigprocmask(SIG_UNBLOCK, &old, &mask)))
		fail("sigprocmask", "SIG_UNBLOCK", ret);

	if(memcmp(&old, &mask, sizeof(sigset_t)))
		fail("mask mismatch", NULL, 0);

	/* Make sure SIG_BLOCK actually blocks signals */

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	if((ret = sys_sigprocmask(SIG_BLOCK, &mask, &old)))
		fail("sigprocmask", "SIG_BLOCK", ret);
	if((ret = sys_kill(pid, SIGINT)) < 0)
		fail("kill", NULL, 0);
	if((ret = sys_nanosleep(&ts, 0)) < 0)
		fail("nanosleep", NULL, ret);

	return 0;
}
