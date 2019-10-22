#ifndef __BITS_SOCKET_H__
#define __BITS_SOCKET_H__

#include <bits/types.h>
#include <bits/socket/unspec.h>

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#define SOCK_SEQPACKET  5
#define SOCK_PACKET    10

#define SOCK_NONBLOCK  04000
#define SOCK_CLOEXEC   02000000

#define SOL_SOCKET      1

#define SO_DEBUG        0x0001
#define SO_REUSEADDR    0x0002
#define SO_KEEPALIVE    0x0009
#define SO_DONTROUTE    0x0005
#define SO_BROADCAST    0x0006
#define SO_LINGER       0x000d
#define SO_REUSEPORT    0x000f
#define SO_PASSCRED     20
#define SO_PEERCRED     21

#define MSG_OOB            (1<<0)
#define MSG_DONTWAIT       (1<<6)
#define MSG_NOSIGNAL       (1<<14)
#define MSG_CMSG_CLOEXEC   (1<<30)

#endif
