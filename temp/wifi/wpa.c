#include <bits/errno.h>

#include <sys/signal.h>
#include <sys/sched.h>
#include <sys/ppoll.h>

#include <sigset.h>
#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

#include "wpa.h"

char* ifname;
int ifindex;
int frequency;
uint8_t bssid[6];
char* ssid;

int signalled;
int tkipgroup;

ERRTAG("wpa");
ERRLIST(NEINVAL NEBUSY NENOENT NEPERM NEFAULT NEINTR NEINPROGRESS NEFAULT
	NENOBUFS NENOLINK NENOTCONN NEOPNOTSUPP NENETDOWN NEALREADY NEMSGSIZE
	NEPROTO NERANGE);

/* Ref. IEEE 802.11-2012 8.4.2.27 RSNE */

const char ies_ccmp[] = {
	0x30, 0x14, /* ies { type = 48, len = 20 } */
	    0x01, 0x00, /* version 1 */
	    0x00, 0x0F, 0xAC, 0x04, /* CCMP group data chipher */
	    0x01, 0x00, /* pairwise chipher suite count */
	    0x00, 0x0F, 0xAC, 0x04, /* CCMP pairwise chipher */
	    0x01, 0x00, /* authentication and key management */
	    0x00, 0x0F, 0xAC, 0x02, /* PSK and RSNA key mgmt */
	    0x00, 0x00, /* preauth capabilities */
};

const char ies_tkip[] = {
	0x30, 0x14,      /* everything's the same, except for: */
	    0x01, 0x00,
	    0x00, 0x0F, 0xAC, 0x02, /* TKIP group data chipher */
	    0x01, 0x00,
	    0x00, 0x0F, 0xAC, 0x04,
	    0x01, 0x00,
	    0x00, 0x0F, 0xAC, 0x02,
	    0x00, 0x00,
};

const char* ies; /* points to either of above */
int iesize;

static void malarm(int ms)
{
	struct itimerval it = {
		.interval = { 0, 0 },
		.value = { 0, 1000*ms }
	};

	sys_setitimer(ITIMER_REAL, &it, NULL);
}

static void sighandler(int sig)
{
	if(signalled)
		_exit(0xFF);

	signalled = 1;
	malarm(1000);

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
	SIGHANDLER(sa, sighandler, 0);
	int ret = 0;

	sigemptyset(&sa.mask);
	sigaddset(&sa.mask, SIGINT);
	sigaddset(&sa.mask, SIGTERM);
	sigaddset(&sa.mask, SIGHUP);

	ret |= sys_sigaction(SIGINT,  &sa, NULL);
	ret |= sys_sigaction(SIGHUP,  &sa, NULL);
	ret |= sys_sigaction(SIGTERM, &sa, NULL);
	ret |= sys_sigaction(SIGALRM, &sa, NULL);

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

static void mode_ccmp_ccmp(void)
{
	ies = ies_ccmp;
	iesize = sizeof(ies_ccmp);
}

static void mode_ccmp_tkip(void)
{
	ies = ies_tkip;
	iesize = sizeof(ies_tkip);
	tkipgroup = 1;
}

/* envp: PSK=001122...EEFF
   argv: wpa wlp1s0 5180 00:11:22:33:44:55 "Blackhole" [ct]

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
	if(!(p = parseint(arg_freq, (int*)&frequency)) || *p)
		fail("invalid frequency:", arg_freq, 0);

	if(!arg_mode || !strcmp(arg_mode, "cc"))
		mode_ccmp_ccmp();
	else if(!strcmp(arg_mode, "ct"))
		mode_ccmp_tkip();
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
		if((ret = sys_ppoll(fds, 2, NULL, NULL)) < 0)
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

int main(int argc, char** argv)
{
	char** envp = argv + argc + 1;
	setup(argc, argv, envp);

	malarm(900);
	authenticate();
	open_rawsock();
	associate();

	malarm(300);
	negotiate_keys();
	upload_ptk();
	upload_gtk();
	cleanup_keys();
	malarm(0);

	poll_netlink_rawsock();

	return 0;
}
