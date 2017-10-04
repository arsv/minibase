#include <bits/ioctl/socket.h>
#include <sys/ioctl.h>

#include <string.h>
#include <format.h>
#include <util.h>

int getifindex(int fd, char* ifname)
{
	struct ifreq ifreq;
	size_t len = strlen(ifname);
	int ifi;
	char* p;

	if((p = parseint(ifname, &ifi)) && !*p)
		return ifi;

	if(len > sizeof(ifreq.name) - 1)
		return -ENAMETOOLONG;

	memset(&ifreq, 0, sizeof(ifreq));
	memcpy(ifreq.name, ifname, len);
	
	long ret = sys_ioctl(fd, SIOCGIFINDEX, &ifreq);

	if(ret < 0)
		return ret;

	return ifreq.ival;
}
