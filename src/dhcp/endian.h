inline static uint16_t htons(uint16_t n)
{
	return ((n & 0xFF) << 8) | ((n >> 8) & 0xFF);
}

inline static uint32_t htonl(uint32_t n)
{
	return   (((n >>  0) & 0xFF) << 24)
	       | (((n >>  8) & 0xFF) << 16)
	       | (((n >> 16) & 0xFF) <<  8)
	       | (((n >> 24) & 0xFF) <<  0);
}

inline static uint16_t ntohs(uint16_t n)
{
	return htons(n);
}

inline static uint32_t ntohl(uint32_t n)
{
	return htonl(n);
}
