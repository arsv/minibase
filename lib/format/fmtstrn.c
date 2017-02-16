#include <format.h>
#include <string.h>
#include <util.h>

char* fmtstrn(char* dst, char* end, const char* src, int len)
{
	char* p = dst;
	const char* q = src;

	if(dst + len < end)
		end = dst + len;

	while(p < end && *q)
		*p++ = *q++;

	return p;
}
