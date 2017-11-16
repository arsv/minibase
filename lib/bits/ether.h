#ifndef __BITS_ETHER_H__
#define __BITS_ETHER_H__

#include <bits/types.h>

#define ETH_P_IP  0x0800
#define ETH_P_PAE 0x888E

struct ethhdr {
	byte dst[6];
	byte src[6];
	uint16_t proto;
} __attribute__((packed));

#endif
