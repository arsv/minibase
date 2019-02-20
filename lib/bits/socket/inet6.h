#include <bits/types.h>

#define PF_INET6 10
#define AF_INET6 10

struct sockaddr_in6 {
	uint16_t family;
	uint16_t port;
	uint32_t flowinfo;
	byte addr[16];
	uint32_t scope;
};
