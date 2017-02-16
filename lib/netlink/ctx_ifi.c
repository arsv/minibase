#include <bits/ioctl/socket.h>
#include <bits/errno.h>
#include <sys/ioctl.h>

#include <string.h>
#include "ctx.h"

int nl_ifindex(struct netlink* nl, const char* ifname)
{
	struct ifreq ifreq;
	int len = strlen(ifname);

	if(len > sizeof(ifreq.name) - 1)
		return -ENAMETOOLONG;

	memset(&ifreq, 0, sizeof(ifreq));
	memcpy(ifreq.name, ifname, len);
	
	long ret = sysioctl(nl->fd, SIOCGIFINDEX, (long)&ifreq);

	if(ret < 0)
		return ret;

	return ifreq.ival;
}
