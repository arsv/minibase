#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/signal.h>
#include <sys/sched.h>

#include <format.h>
#include <errtag.h>
#include <util.h>

#include "common.h"
#include "findblk.h"

ERRTAG("findblk");

void quit(const char* msg, char* arg, int err)
{
	fail(msg, arg, err);
}

int check_keyindex(int kidx)
{
	(void)kidx;
	return -EINVAL;
}

static void sighandler(int sig)
{
	switch(sig) {
		case SIGALRM:
			quit("timeout waiting for devices", NULL, 0);
	}
}

static void setup(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTORER,
		.restorer = sigreturn
	};

	sys_sigaction(SIGALRM, &sa, NULL);
}

int main(int argc, char** argv)
{
	(void)argv;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	setup();
	load_config();
	sys_alarm(5);

	open_udev();
	scan_devs();
	wait_udev();

	link_parts();

	return 0;
}
