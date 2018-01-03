#include <format.h>

char* fmtx32(char* buf, char* end, uint32_t num)
{
	static const char digits[] = "0123456789ABCDEF";

	int len = 0;
	long n;

	for(len = 1, n = num; n >= 0x10; len++)
		n >>= 4;

	int i;
	char* e = buf + len;
	char* p = e - 1; /* len >= 1 so e > buf */

	for(i = 0; i < len; i++, p--, num >>= 4)
		if(p < end)
			*p = digits[num & 0x0F];

	return e;
}

#if BITS == 32

char* fmtxint(char* buf, char* end, uint num)
	__attribute__((alias("fmtx32")));

char* fmtxlong(char* buf, char* end, ulong num)
	__attribute__((alias("fmtx32")));

#endif
