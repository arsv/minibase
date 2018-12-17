#ifndef __BITS_SOCKET_H__
#define __BITS_SOCKET_H__

#include <bits/types.h>
#include <bits/socket/unspec.h>

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#define SOCK_SEQPACKET  5
#define SOCK_PACKET    10

#define SOCK_NONBLOCK  (1<<11)
#define SOCK_CLOEXEC   (1<<19)

#define SOL_SOCKET      1

#define SO_DEBUG        0x0001
#define SO_REUSEADDR    0x0004
#define SO_KEEPALIVE    0x0008
#define SO_DONTROUTE    0x0010
#define SO_BROADCAST    0x0020
#define SO_LINGER       0x0080
#define SO_REUSEPORT    0x0200
#define SO_PASSCRED     17
#define SO_PEERCRED     18
#define SO_BINDTODEVICE 25

#define MSG_OOB            (1<<0)
#define MSG_DONTWAIT       (1<<6)
#define MSG_NOSIGNAL       (1<<14)
#define MSG_CMSG_CLOEXEC   (1<<30)

#endif
