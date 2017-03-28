#include "dhcp_udp.h"

/* Generic UDP/IP header routines. Does not look like these will be
   used anywhere else any time soon, at least not in this form. */

static uint32_t checksum(void* addr, int len)
{
	uint8_t* buf = addr;
	uint8_t* end = buf + len - 1;
	uint8_t* p = buf;

	uint32_t sum = 0;

	for(p = buf; p < end; p += 2) {
		sum += *(p+0) << 8;
		sum += *(p+1) << 0;
	} if(len & 1) {
		sum += *(p+0) << 8;
	}

	return sum;
}

static uint16_t flipcarry(uint32_t sum)
{
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = (sum & 0xFFFF) + (sum >> 16);
	return (~sum & 0xFFFF);
}

uint16_t ipchecksum(void* addr, int len)
{
	return flipcarry(checksum(addr, len));
}

uint16_t udpchecksum(void* addr, int len, void* ips)
{
	uint32_t sum = checksum(addr, len);
	sum += checksum(ips, 2*4);
	sum += len + IPPROTO_UDP;
	return flipcarry(sum);
}
