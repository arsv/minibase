#include "memcpy.h"
#include "fmtstr.h"

char* fmtstrn(char* dst, char* end, const char* src, int len)
{
	if(dst + len >= end)
		len = end - dst;

	memcpy(dst, src, len);

	return dst + len;
}
