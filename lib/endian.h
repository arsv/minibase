#include <bits/types.h>

inline static uint16_t swabs(uint16_t n)
{
	return ((n & 0xFF) << 8) | ((n >> 8) & 0xFF);
}

inline static uint32_t swabl(uint32_t n)
{
	return   (((n >>  0) & 0xFF) << 24)
	       | (((n >>  8) & 0xFF) << 16)
	       | (((n >> 16) & 0xFF) <<  8)
	       | (((n >> 24) & 0xFF) <<  0);
}

inline static uint64_t swabx(uint64_t n)
{
	return   (((n >> 0*8) & 0xFF) << 7*8)
	       | (((n >> 1*8) & 0xFF) << 6*8)
	       | (((n >> 2*8) & 0xFF) << 5*8)
	       | (((n >> 3*8) & 0xFF) << 4*8)
	       | (((n >> 4*8) & 0xFF) << 3*8)
	       | (((n >> 5*8) & 0xFF) << 2*8)
	       | (((n >> 6*8) & 0xFF) << 1*8)
	       | (((n >> 7*8) & 0xFF) << 0*8);
}

#ifdef BIGENDIAN

inline static uint16_t htons(uint16_t n) { return n; }
inline static uint32_t htonl(uint32_t n) { return n; }
inline static uint64_t htonx(uint64_t n) { return n; }

inline static uint16_t ntohs(uint16_t n) { return n; }
inline static uint32_t ntohl(uint32_t n) { return n; }
inline static uint64_t ntohl(uint64_t n) { return n; }

inline static uint16_t itohs(uint16_t n) { return swabs(n); }
inline static uint32_t itohl(uint32_t n) { return swabl(n); }

inline static uint32_t htois(uint16_t n) { return swabs(n); }
inline static uint32_t htoil(uint32_t n) { return swabl(n); }

#else

inline static uint16_t htons(uint16_t n) { return swabs(n); }
inline static uint32_t htonl(uint32_t n) { return swabl(n); }
inline static uint64_t htonx(uint64_t n) { return swabx(n); }

inline static uint16_t ntohs(uint16_t n) { return swabs(n); }
inline static uint32_t ntohl(uint32_t n) { return swabl(n); }
inline static uint64_t ntohx(uint64_t n) { return swabx(n); }

inline static uint16_t itohs(uint16_t n) { return n; }
inline static uint32_t itohl(uint32_t n) { return n; }

inline static uint32_t htois(uint16_t n) { return n; }
inline static uint32_t htoil(uint32_t n) { return n; }

#endif
