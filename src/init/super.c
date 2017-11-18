#include <sys/creds.h>
#include <sys/fpath.h>
#include <sys/file.h>
#include <sys/proc.h>

#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "super.h"

char* confdir;
char** environ;
char rbcode;

static short flagged;
static char reboot[50];

static int exec_into_reboot(void)
{
	if(!reboot[0]) return -1;

	char arg[] = { '-', rbcode, '\0' };
	char* argv[] = { reboot, arg, NULL };

	int ret = sys_execve(*argv, argv, environ);

	report("exec", *argv, ret);
	
	return -1; /* cause kernel panic */
}

static void setup_args(int argc, char** argv)
{
	if(argc < 2)
		goto out;

	char* arg = argv[1];
	unsigned len = strlen(arg);

	if(len > sizeof(reboot) - 1) {
		report("command too long:", arg, 0);
		goto out;
	}

	memcpy(reboot, arg, len);
	reboot[len] = '\0';
out:
	for(int i = 1; i < argc; i++)
		memzero(argv[i], strlen(argv[i]));
}

void request(int flags)
{
	flagged |= flags;
}

static int need_to(int flag)
{
	int ret = flagged & flag;

	flagged &= ~flag;

	return ret;
}

int main(int argc, char** argv, char** envp)
{
	environ = envp;
	confdir = CONFDIR;

	setup_args(argc, argv);

	setup_heap();
	setup_ctrl();

	if(setup_signals())
		goto reboot;
	if(reload_procs())
		goto reboot;

	check_procs();
	update_poll_fds();

	while(!rbcode) {
		wait_poll();

		if(need_to(F_WAIT_PIDS))
			wait_pids();
		if(need_to(F_CHECK_PROCS))
			check_procs();
		if(need_to(F_SETUP_CTRL))
			setup_ctrl();
		if(need_to(F_RELOAD_PROCS))
			reload_procs();
		if(need_to(F_FLUSH_HEAP))
			flush_heap();
		if(need_to(F_TRIM_RING))
			trim_ring_area();
		if(need_to(F_UPDATE_PFDS))
			update_poll_fds();
	}
reboot:
	sys_unlink(CONTROL);

	return exec_into_reboot();
};
