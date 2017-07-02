#ifndef __BITS_SOCKET_H__
#define __BITS_SOCKET_H__

#include <bits/types.h>

#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3
#define SOCK_CLOEXEC   02000000
#define SOCK_NONBLOCK  04000

#define SOL_SOCKET      1
#define SO_PEERCRED     17

#define SHUT_WR 1

#define MSG_OOB            (1<<0)
#define MSG_DONTWAIT       (1<<6)
#define MSG_CMSG_CLOEXEC  (1<<30)

#define AF_UNSPEC      0
#define PF_UNSPEC      0

struct sockaddr {
	uint16_t family;
	char data[14];
};

struct ucred {
	pid_t pid;
	uid_t uid;
	gid_t gid;
};

#endif
