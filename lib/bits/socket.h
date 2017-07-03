#ifndef __BITS_SOCKET_H__
#define __BITS_SOCKET_H__

#include <bits/types.h>
#include <bits/socket/unspec.h>

#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3
#define SOCK_CLOEXEC   02000000
#define SOCK_NONBLOCK  04000

#define SOL_SOCKET      1
#define SO_PEERCRED     17

#define MSG_OOB            (1<<0)
#define MSG_DONTWAIT       (1<<6)
#define MSG_CMSG_CLOEXEC  (1<<30)

struct ucred {
	pid_t pid;
	uid_t uid;
	gid_t gid;
};

#endif
