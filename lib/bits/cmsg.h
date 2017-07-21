#ifndef __BITS_CMSG_H__
#define __BITS_CMSG_H__

#include <bits/types.h>

struct cmsghdr {
	size_t len;
	int level;
	int type;
	char data[];
};

#endif
