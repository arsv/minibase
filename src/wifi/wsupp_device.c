#include <bits/ioctl/socket.h>
#include <bits/arp.h>
#include <sys/ioctl.h>

#include <string.h>
#include <printf.h>
#include <util.h>

#include "wsupp.h"

int ifindex;
char ifname[32];
byte ifaddr[6];

#define IFF_UP (1<<0)

int bring_iface_up(void)
{
	int ret, fd = netlink;
	char* name = ifname;
	uint nlen = strlen(ifname);
	struct ifreq ifr;

	if(nlen > sizeof(ifr.name))
		return -ENAMETOOLONG;

	memzero(&ifr, sizeof(ifr));
	memcpy(ifr.name, name, nlen);

	if((ret = sys_ioctl(fd, SIOCGIFFLAGS, &ifr)) < 0) {
		warn("ioctl SIOCGIFFLAGS", name, ret);
		return ret;
	}

	if(ifr.ival & IFF_UP)
		return 0;

	ifr.ival |= IFF_UP;

	if((ret = sys_ioctl(fd, SIOCSIFFLAGS, &ifr)) < 0) {
		warn("ioctl SIOCSIFFLAGS", name, ret);
		return ret;
	}

	return 0;
}

static void clear_device(void)
{
	ifindex = 0;
	memzero(ifname, sizeof(ifname));
	memzero(ifaddr, sizeof(ifaddr));
}

void reset_device(void)
{
	reset_eapol_state();
	clear_scan_table();

	authstate = AS_IDLE;
	scanstate = SS_IDLE;

	close_rfkill();
	close_rawsock();
	close_netlink();
	clr_timer();

	clear_device();
}

int set_device(char* name)
{
	int ret;

	if(ifindex > 0)
		reset_device();

	if((ret = open_netlink()) < 0) /* need some socket for ioctls */
		return ret;

	int fd = netlink;
	uint nlen = strlen(name);
	struct ifreq ifr;

	if(nlen > sizeof(ifr.name) || nlen > sizeof(ifname) - 1)
		return -ENAMETOOLONG;

	memzero(&ifr, sizeof(ifr));
	memcpy(ifr.name, name, nlen);

	if((ret = sys_ioctl(fd, SIOCGIFINDEX, &ifr)) < 0) {
		warn("ioctl SIOCGIFINDEX", name, ret);
		goto err;
	}

	int ifi = ifr.ival;

	if((ret = sys_ioctl(fd, SIOCGIFHWADDR, &ifr)) < 0) {
		warn("ioctl SIOCGIFHWADDR", name, ret);
		goto err;
	}
	if(ifr.addr.family != ARPHRD_ETHER) {
		warn("unexpected hwaddr family on", name, 0);
		ret = -EINVAL;
		goto err;
	}

	ifindex = ifi;
	memcpy(ifname, name, nlen + 1);
	memcpy(ifaddr, ifr.addr.data, 6);

	if((ret = bring_iface_up()) < 0)
		goto err;
	if((ret = start_void_scan()) < 0)
		goto err;

	return 0;
err:
	clear_device();
	return ret;
}
