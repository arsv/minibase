#include <format.h>
#include <string.h>
#include <util.h>

char* fmtraw(char* dst, char* end, const char* src, int len)
{
	if(dst + len >= end)
		len = end - dst;
	if(len <= 0)
		return dst;

	memcpy(dst, src, len);

	return dst + len;
}
