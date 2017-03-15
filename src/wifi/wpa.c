#include <bits/errno.h>

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

ERRTAG = "wpa";
ERRLIST = {
	REPORT(EINVAL), REPORT(EBUSY), REPORT(ENOENT), REPORT(EPERM),
	REPORT(EFAULT), REPORT(EINTR), REPORT(EINPROGRESS), REPORT(EFAULT),
	REPORT(ENOBUFS), REPORT(ENOLINK), REPORT(ENOTCONN), REPORT(EOPNOTSUPP),
	REPORT(ENETDOWN), REPORT(EALREADY), REPORT(EMSGSIZE), REPORT(EPROTO),
	REPORT(ERANGE), RESTASNUMBERS
};

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
   argv: wpa wlp1s0 5180 00:11:22:33:44:55 "Blackhole" 

   Tune wlp1s0 to 5180MHz and connect to station 00:11:22:33:44:55
   named "Blackhole" using supplied PSK. */

void setup(int argc, char** argv, char** envp)
{
	char* p;
	int i = 1;

	if(i + 4 > argc)
		fail("too few arguments", NULL, 0);

	char* arg_dev = argv[i++];
	char* arg_freq = argv[i++];
	char* arg_bssid = argv[i++];
	char* arg_ssid = argv[i++];

	if(!(p = parsemac(arg_bssid, bssid)) || *p)
		fail("invalid bssid:", arg_bssid, 0);
	if(!(p = parseint(arg_freq, &frequency)) || *p)
		fail("invalid frequency:", arg_freq, 0);

	setup_psk(envp);
	setup_netlink();

	if((ifindex = resolve_ifname(arg_dev)) < 0)
		fail("unknown device", arg_dev, 0);

	ifname = arg_dev;
	ssid = arg_ssid;
}

int main(int argc, char** argv, char** envp)
{
	setup(argc, argv, envp);

	authenticate();
	open_rawsock();
	associate();
	close_netlink();

	negotiate_keys();
	open_netlink();
	upload_ptk();
	upload_gtk();
	cleanup_keys();
	close_netlink();

	while(1) {
		group_rekey();
		open_netlink();
		upload_gtk();
		close_netlink();
	}

	return 0;
}
