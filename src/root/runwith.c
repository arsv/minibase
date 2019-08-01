#include <sys/proc.h>
#include <sys/signal.h>

#include <string.h>
#include <util.h>
#include <main.h>

/* This tiny tool is only meant for a single use case: keeping udevmod
   running in background while {find,pass}blk are waiting for devices.

   Typical sequence looks like this: the bus finds a device and sends
   MODALIAS event, udevmod receives the event and loads the module,
   the module starts and sends DEVNAME event, {find,pass}blk receive
   the event and do their thing.

   Outside of initrd, udevmod runs as a regular supervised service, but
   within initrd this wouldn't work because the second process in the
   pair (findblk or passblk) is not a service and should not be treated
   as a service.

   This tool could have been an msh built-in, maybe, but there's really no
   use whatsoever for this outside of that single line in initrd/init, and
   it would add sort-of supervision functions to msh which it does not need
   otherwise. Msh really isn't designed for handling more than one process,
   so this became a standalone executable. */

ERRTAG("runwith");

static int count_remaining(int* pids, int npids)
{
	int count = 0, i = 0;

	while(i < npids)
		if(pids[i++] > 0)
			count++;

	return count;
}

static int mark_dead(int* pids, int npids, int pid)
{
	int i;

	for(i = 0; i < npids; i++)
		if(pids[i] == pid)
			break;

	if(i >= npids)
		return -1;

	pids[i] = -1;

	return i;
}

static int wait_first(int* pids, int npids, int* status)
{
	int pid;

	if((pid = sys_waitpid(-1, status, 0)) < 0)
		fail("waitpid", NULL, pid);

	return mark_dead(pids, npids, pid);
}

static void kill_wait_rest(int* pids, int npids)
{
	int pid, status, i;

	for(i = 0; i < npids; i++)
		if(pids[i] > 0)
			sys_kill(pids[i], SIGTERM);

	while(count_remaining(pids, npids)) {
		if((pid = sys_waitpid(-1, &status, 0)) < 0)
			fail("waitpid", NULL, pid);

		mark_dead(pids, npids, pid);
	}

}

static int spawn(int argc, char** argv, char** envp)
{
	int pid;
	char* args[argc+1];

	memcpy(args, argv, argc*sizeof(char*));
	args[argc] = NULL;

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);

	if(pid == 0) {
		int ret = execvpe(*args, args, envp);
		if(ret) warn("exec", *args, ret);
		_exit(ret ? -1 : 0);
	}

	return pid;
}

int main(int argc, char** argv)
{
	char** envp = argv + argc + 1;
	int pids[2];
	int status, ret = -1;
	int npids = ARRAY_SIZE(pids);

	if(argc < 3)
		fail("too few arguments", NULL, 0);

	pids[0] = spawn(1, argv + 1, envp);
	pids[1] = spawn(argc - 2, argv + 2, envp);

	if(wait_first(pids, npids, &status))
		ret = -1;
	else if(WIFEXITED(status))
		ret = WEXITSTATUS(status);
	else
		ret = -1;

	kill_wait_rest(pids, npids);

	return ret;
}
