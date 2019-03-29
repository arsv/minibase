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

static void stop_running(int pid)
{
	int ret, status;
	struct itimerval old, itv = {
		.interval = { 0, 0 },
		.value = { 1, 0 }
	};

	sys_kill(pid, SIGTERM);

	sys_setitimer(0, &itv, &old);
	ret = sys_waitpid(pid, &status, 0);
	sys_setitimer(0, &old, NULL);

	if(ret < 0)
		warn("wait", NULL, ret);

	running = 0;
}

void trigger_dhcp(void)
{
	char* script = HERE "/etc/net/wifi-wpa";
	int pid = running;

	if(pid > 0)
		stop_running(pid);

	if(sys_access(script, X_OK) < 0)
		return;

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return;
	}

	if(pid == 0) {
		char* args[] = { script, ifname, NULL };
		int ret = sys_execve(*args, args, environ);
		warn("exec", script, ret);
		_exit(-1);
	}

	running = pid;
}
