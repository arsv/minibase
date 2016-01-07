#ifndef __BITS_UNAME_H__
#define __BITS_UNAME_H__

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

#endif
