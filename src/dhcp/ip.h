#include <bits/ints.h>

#define IPVERSION 4
#define IPV4IHL5 0x45 /* version 4, header len 5x32bit */
#define IPPROTO_UDP 17
#define IPDEFTTL 64

struct iphdr {
	uint8_t  verihl;
	uint8_t  tos;
	uint16_t tot_len;
	uint16_t id;
	uint16_t frag_off;
	uint8_t  ttl;
	uint8_t  protocol;
	uint16_t check;
	uint8_t saddr[4];
	uint8_t daddr[4];
} __attribute__((packed));

struct udphdr {
	uint16_t source;
	uint16_t dest;
	uint16_t len;
	uint16_t check;
} __attribute__((packed));

uint16_t ipchecksum(void *addr, int len);
uint16_t udpchecksum(void* addr, int len, void* ips);
