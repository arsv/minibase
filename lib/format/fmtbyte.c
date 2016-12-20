#include <format.h>

char* fmtbyte(char* dst, char* end, char c)
{
	static const char digits[] = "0123456789ABCDEF";

	if(dst < end)
		*dst++ = digits[(c >> 4) & 0x0F];
	if(dst < end)
		*dst++ = digits[(c >> 0) & 0x0F];

	return dst;
}
