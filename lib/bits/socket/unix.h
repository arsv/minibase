#include <bits/ints.h>

#define AF_UNIX 1
#define PF_UNIX 1

struct sockaddr_un {
	uint16_t family;
	char path[108];
};
