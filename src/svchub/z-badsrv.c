#include <sys/file.h>
#include <sys/sched.h>

#include <string.h>

/* Always-failing stub service for testing failure handling in svchub */

static void say(char* msg)
{
	int len = strlen(msg);
	sys_write(STDOUT, msg, len);
}

int main(void)
{
	struct timespec ts = { 1, 0 };

	say("badsrv: starting\n");
	say("badsrv: some output here\n");
	sys_nanosleep(&ts, NULL);
	say("badsrv: failed, exiting\n");

	return 0xFF;
}
