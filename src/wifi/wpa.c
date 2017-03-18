#include <bits/errno.h>
#include <sys/sigaction.h>
#include <sys/alarm.h>
#include <sys/ppoll.h>
#include <sys/_exit.h>

#include <sigset.h>
#include <string.h>
#include <format.h>
#include <fail.h>
#include <util.h>

#include "wpa.h"

char* ifname;
int ifindex;
int frequency;
uint8_t bssid[6];
char* ssid;

int signalled;
int quitting;

int tkipgroup;

ERRTAG = "wpa";
ERRLIST = {
	REPORT(EINVAL), REPORT(EBUSY), REPORT(ENOENT), REPORT(EPERM),
	REPORT(EFAULT), REPORT(EINTR), REPORT(EINPROGRESS), REPORT(EFAULT),
	REPORT(ENOBUFS), REPORT(ENOLINK), REPORT(ENOTCONN), REPORT(EOPNOTSUPP),
	REPORT(ENETDOWN), REPORT(EALREADY), REPORT(EMSGSIZE), REPORT(EPROTO),
	REPORT(ERANGE), RESTASNUMBERS
};

/* Signals and netlink code do not mix well. Success of disconnect() relies
   heavily on atomicity of whatever NL call might have been interrupted.
   Still, this is the best we can do. If disconnect fails, well it's a bad
   day then.

   This should happen very fast, unconditionally. Alarm is only set for
   signalled exits, and even then only for extra reliability. Note TERM,
   INT and HUP are blocked within sighandler, so after the first one it
   can only be ALRM.

   This whole thing is *only* to make sure the connection goes down with
   the process. Just exiting would leave the iface running. */

void quit(const char* msg, const char* arg, int err)
{
	if(!quitting) {
		quitting = 1;
		disconnect();
	}

	if(msg || arg)
		fail(msg, arg, err);
	else
		_exit(0);
}

static void sighandler(int sig)
{
	if(signalled)
		_exit(0xFF);

	signalled = 1;
	sysalarm(1);

	const char* msg;

	if(sig == SIGINT || sig == SIGTERM)
		msg = NULL;
	else if(sig == SIGALRM)
		msg = "timeout";
	else
		msg = "signalled";

	quit(msg, NULL, 0);
}

static void setup_signals(void)
{
	int ret = 0;

	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = SA_RESTORER,
		.sa_restorer = sigreturn,
	};

	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGINT);
	sigaddset(&sa.sa_mask, SIGTERM);
	sigaddset(&sa.sa_mask, SIGHUP);

	ret |= syssigaction(SIGINT,  &sa, NULL);
	ret |= syssigaction(SIGHUP,  &sa, NULL);
	ret |= syssigaction(SIGTERM, &sa, NULL);
	ret |= syssigaction(SIGALRM, &sa, NULL);

	if(ret) fail("signal setup failed", NULL, 0);
}

/* This process should be (mildly) privileged to be able to configure
   wdevs, so its envp should not be readable for anyone we should be
   concerned about. However, PSK is still a key, so we wipe it out of
   envp as soon as it gets parsed. */

static void setup_psk(char** envp)
{
	char* arg = getenv(envp, "PSK");
	int len = strlen(arg);
	int req = sizeof(PSK); /* 32 */

	char* p;

	if(!arg)
		fail("no PSK supplied", NULL, 0);
	if(len != 2*req)
		fail("PSK must be 32 bytes long", NULL, 0);
	if(!(p = parsebytes(arg, PSK, req)) || *p)
		fail("invalid PSK", NULL, 0);

	memset(arg, 'x', len); /* PSK=xxxxxx...xxxx */
}

/* envp: PSK=001122...EEFF
   argv: wpa wlp1s0 5180 00:11:22:33:44:55 "Blackhole" [tkip]

   Tune wlp1s0 to 5180MHz and connect to station 00:11:22:33:44:55
   named "Blackhole" using supplied PSK. */

static void setup(int argc, char** argv, char** envp)
{
	char* p;
	int i = 1;

	if(i + 4 > argc)
		fail("too few arguments", NULL, 0);

	char* arg_dev = argv[i++];
	char* arg_freq = argv[i++];
	char* arg_bssid = argv[i++];
	char* arg_ssid = argv[i++];
	char* arg_mode = i < argc ? argv[i++] : NULL;

	if(!(p = parsemac(arg_bssid, bssid)) || *p)
		fail("invalid bssid:", arg_bssid, 0);
	if(!(p = parseint(arg_freq, &frequency)) || *p)
		fail("invalid frequency:", arg_freq, 0);

	if(!arg_mode || !strcmp(arg_mode, "ccmp"))
		;
	else if(!strcmp(arg_mode, "tkip"))
		tkipgroup = 1;
	else
		fail("invalid mode", arg_mode, 0);

	setup_psk(envp);
	setup_netlink();

	if((ifindex = resolve_ifname(arg_dev)) < 0)
		fail("unknown device", arg_dev, 0);

	ifname = arg_dev;
	ssid = arg_ssid;

	setup_signals();
}

static void poll_netlink_rawsock(void)
{
	int ret;

	struct pollfd fds[2] = {
		{ .fd = netlink, .events = POLLIN },
		{ .fd = rawsock, .events = POLLIN }
	};

	while(1) {
		if((ret = sysppoll(fds, 2, NULL, NULL)) < 0)
			quit("ppoll", NULL, ret);

		if(fds[0].revents & ~POLLIN)
			quit("netlink dropped", NULL, 0);
		if(fds[0].revents & POLLIN)
			pull_netlink();

		if(fds[1].revents & ~POLLIN)
			quit("rawsock dropped", NULL, 0);
		if(fds[1].revents & POLLIN)
			if(group_rekey())
				upload_gtk();
	};
}

int main(int argc, char** argv, char** envp)
{
	setup(argc, argv, envp);

	sysalarm(1);

	authenticate();
	open_rawsock();
	associate();

	negotiate_keys();
	upload_ptk();
	upload_gtk();
	cleanup_keys();

	sysalarm(0);

	poll_netlink_rawsock();

	return 0;
}
