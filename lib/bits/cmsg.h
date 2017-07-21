#ifndef __BITS_CMSG_H__
#define __BITS_CMSG_H__

#include <bits/types.h>

#define SCM_RIGHTS      1
#define SCM_CREDENTIALS 2

struct cmsghdr {
	size_t len;
	int level;
	int type;
	char data[];
};

struct cmsgfd {
	size_t len;
	int level;
	int type;
	int fd;
	int pad;
};

#endif
