#include <format.h>

char* fmtchar(char* dst, char* end, char c)
{
	if(dst < end)
		*dst++ = c;
	return dst;
}
