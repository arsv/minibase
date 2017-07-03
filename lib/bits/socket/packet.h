#ifndef __BITS_PACKET_H__
#define __BITS_PACKET_H__

#include <bits/ints.h>

#define AF_PACKET 17
#define PF_PACKET 17

#define PACKET_HOST        0
#define PACKET_BROADCAST   1
#define PACKET_MULTICAST   2
#define PACKET_OTHERHOST   3
#define PACKET_OUTGOING    4
#define PACKET_LOOPBACK    5
#define PACKET_USER        6
#define PACKET_KERNEL      7

struct sockaddr_ll {
	uint16_t family;
	uint16_t protocol;
	int32_t  ifindex;
	uint16_t hatype;
	uint8_t  pkttype;
	uint8_t  halen;
	uint8_t  addr[8];
};

#endif
