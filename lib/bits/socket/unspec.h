#ifndef __BITS_SOCKET_UNSPEC__
#define __BITS_SOCKET_UNSPEC__

#include <bits/types.h>

#define AF_UNSPEC      0
#define PF_UNSPEC      0

struct sockaddr {
	uint16_t family;
	char data[14];
};

#endif
