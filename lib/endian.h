#include <bits/ints.h>

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

#ifdef BIGENDIAN

inline static uint16_t htons(uint16_t n) { return n; }
inline static uint32_t htonl(uint32_t n) { return n; }

inline static uint16_t ntohs(uint16_t n) { return n; }
inline static uint32_t ntohl(uint32_t n) { return n; }

inline static uint16_t itohs(uint16_t n) { return swabs(n); }
inline static uint32_t itohl(uint32_t n) { return swabl(n); }

inline static uint32_t htois(uint16_t n) { return swabs(n); }
inline static uint32_t htoil(uint32_t n) { return swabl(n); }

#else

inline static uint16_t htons(uint16_t n) { return swabs(n); }
inline static uint32_t htonl(uint32_t n) { return swabl(n); }

inline static uint16_t ntohs(uint16_t n) { return swabs(n); }
inline static uint32_t ntohl(uint32_t n) { return swabl(n); }

inline static uint16_t itohs(uint16_t n) { return n; }
inline static uint32_t itohl(uint32_t n) { return n; }

inline static uint32_t htois(uint16_t n) { return n; }
inline static uint32_t htoil(uint32_t n) { return n; }

#endif
