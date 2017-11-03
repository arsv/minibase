#include <format.h>

char* fmtbyte(char* p, char* e, char c)
{
	static const char digits[] = "0123456789ABCDEF";

	if(p < e)
		*p++ = digits[(c >> 4) & 0x0F];
	if(p < e)
		*p++ = digits[(c >> 0) & 0x0F];

	return p;
}
