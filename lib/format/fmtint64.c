#include <format.h>

/* Format 64-bit integers. Since 64-bit arches will pass shorted
   integers in registers anyway, alias fmt32 to theese as well. */

static char* fmtint64(char* buf, char* end, int minus, uint64_t num)
{
	int len = 0;
	uint64_t n;

	for(len = 1, n = num; n >= 10; len++)
		n /= 10;

	int i;
	char* e = buf + len + minus;
	char* p = e - 1; /* len >= 1 so e > buf */
	
	for(i = 0; i < len; i++, p--, num /= 10)
		if(p < end)
			*p = '0' + num % 10;
	if(minus) *p = '-';

	return e; 
}

char* fmti64(char* buf, char* end, int64_t num)
{
	return fmtint64(buf, end, num < 0, num < 0 ? -num : num);
}

char* fmtu64(char* buf, char* end, uint64_t num)
{
	return fmtint64(buf, end, 0, num);
}

#if BITS == 64

char* fmti32(char* buf, char* end, int32_t num)
{
	return fmtint64(buf, end, num < 0, num < 0 ? -num : num);
}

char* fmtu32(char* buf, char* end, uint32_t num)
{
	return fmtint64(buf, end, 0, num);
}

char* fmtlong(char* buf, char* end, long num)
	__attribute__((alias("fmti64")));
char* fmtulong(char* buf, char* end, unsigned long num)
	__attribute__((alias("fmtu64")));

#endif
