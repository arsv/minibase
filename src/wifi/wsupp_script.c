#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/timer.h>

#include <util.h>

#include "common.h"
#include "wsupp.h"

/* For regular wired links, dhcp client gets run once the link reports carrier
   acquisition (IFF_RUNNING), which can be done in ifmon based solely on the
   link state change event. This does not work with 802.11: carrier up means
   the link is associated, but regular packets are not allowed through until
   EAPOL exchange is completed. Letting ifmon handle wifi link would result
   in the first DHCPREQUEST packet getting sent before the link is truly ready,
   incurring unnecessary resend timeout.

   To get around this, we let wsupp itself spawn dhcp once the link is ready.

   Given the way current iteration of the dhcp client works, we should expect
   the spawned process to linger while the link is active. When the link goes
   down, it should be killed, with SIGTERM or SIGINT depending on whether the
   disconnect was spontaneous or commanded -- see ../net3 docs on this. */

int running;

static int kill_script(int sig)
{
	int pid = running;

	if(pid <= 0) return -ESRCH;

	return sys_kill(pid, sig);
}

int sighup_script(void)
{
	return kill_script(SIGHUP);
}

int sigint_script(void)
{
	return kill_script(SIGINT);
}

void force_script(void)
{
	int pid = running;

	if(pid <= 0) return;

	sys_kill(pid, SIGTERM);

	running = 0;
}

void stop_wait_script(void)
{
	int pid = running;
	int ret, status;

	if(pid <= 0)
		return;
	if((ret = sys_kill(pid, SIGTERM)) < 0)
		return;

	(void)sys_waitpid(pid, &status, 0);
}

void check_script(void)
{
	int pid, status;

	if((pid = sys_waitpid(-1, &status, WNOHANG)) <= 0)
		return;
	if(pid != running)
		return;

	running = 0;

	script_exit(status);
}

int spawn_script(void)
{
	char* script = HERE "/etc/net/wifi-link";
	int ret, pid;

	if(running > 0)
		return -EBUSY;
	if((ret = sys_access(script, X_OK)) < 0)
		return 0;

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return ret;
	}

	if(pid == 0) {
		char* args[] = { script, ifname, NULL };
		int ret = sys_execve(*args, args, environ);
		warn("exec", script, ret);
		_exit(-1);
	}

	running = pid;

	return pid;
}
