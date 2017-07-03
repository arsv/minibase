#include <bits/socket/unspec.h>

#define SIOCGIFNAME	0x8910
#define SIOCGIFHWADDR	0x8927
#define SIOCGIFINDEX	0x8933

#define IFNAMESIZ 16

struct ifreq {
	char name[IFNAMESIZ];
	union {
		int ival;
		struct sockaddr addr;
		char _[64]; /* 34, plus some slack */
	};
};
