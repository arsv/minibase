#include <bits/types.h>
#include <format.h>

/* Format 32-bit integers. Only matters for 32-bit arches where
   it's the native length and i64 requires additional code.

   64-bit arches use 64-bit formatters for all integers. */

#if BITS == 32

static char* fmtint32(char* buf, char* end, int minus, uint32_t num)
{
	int len = 0;
	uint32_t n;

	for(len = 1, n = num; n >= 10; len++)
		n /= 10;

	int i;
	char* e = buf + len + minus;
	char* p = e - 1; /* len >= 1 so e > buf */

	for(i = 0; i < len; i++, p--, num /= 10)
		if(p < end)
			*p = '0' + num % 10;
	if(minus) *p = '-';

	return e > end ? end : e;
}

char* fmti32(char* buf, char* end, int32_t num)
{
	return fmtint32(buf, end, num < 0, num < 0 ? -num : num);
}

char* fmtu32(char* buf, char* end, uint32_t num)
{
	return fmtint32(buf, end, 0, num);
}

char* fmtlong(char* buf, char* end, long num)
	__attribute__((alias("fmti32")));
char* fmtulong(char* buf, char* end, unsigned long num)
	__attribute__((alias("fmtu32")));

char* fmtint(char* buf, char* end, int num)
	__attribute__((alias("fmti32")));
char* fmtuint(char* buf, char* end, unsigned num)
	__attribute__((alias("fmtu32")));

#endif
