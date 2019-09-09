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

/* For regular wired links, dhcp gets run once the link reports carrier
   acquisition (IFF_RUNNING). This does not work with 802.11: carrier
   means the link is associated, but regular packets are not allowed
   through until EAPOL exchange is completed. Running dhcp concurrently
   with EAPOL means the first DHCPREQUEST packet often gets lost,
   which in turn means unnecessary resend timeout.

   There's no way for ifmon to detect the end of EAPOL exchnage on its
   own, in part because key installation does not seem to generate any
   notifications whatsoever, and in part because ifmon has no idea whether
   the keys will be installed at all (we may be running unencrypted link).

   So the workaround here is to suppress normal dhcp logic for wifi links,
   and let EAPOL code notify ifmon when it's ok to start dhcp. */

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
