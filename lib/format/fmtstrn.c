#include <format.h>
#include <string.h>
#include <util.h>

char* fmtstrn(char* dst, char* end, const char* src, int len)
{
	if(dst + len >= end)
		len = end - dst;

	memcpy(dst, src, len);

	return dst + len;
}
