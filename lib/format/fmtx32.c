#include <format.h>

char* fmtx32(char* buf, char* end, uint32_t num)
{
	static const char digits[] = "0123456789ABCDEF";

	ulong len = 1;
	ulong n = num;

	for(; n >= 0x10; n >>= 4)
		len++;

	ulong i;
	char* e = buf + len;
	char* p = e - 1; /* len >= 1 so e > buf */

	for(i = 0; i < len; i++) {
		if(p < end)
			*p = digits[num & 0x0F];
		if(p >= buf)
			p--;
		num >>= 4;
	}

	return e < end ? e : end;
}

#if BITS == 32

char* fmtxint(char* buf, char* end, uint num)
	__attribute__((alias("fmtx32")));

char* fmtxlong(char* buf, char* end, ulong num)
	__attribute__((alias("fmtx32")));

#endif
