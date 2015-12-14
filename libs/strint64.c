#include "strint64.h"

static char* strint64(char* buf, char* end, int minus, uint64_t num)
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

char* stri64(char* buf, char* end, int64_t num)
{
	return strint64(buf, end, num < 0, num < 0 ? -num : num);
}

char* stru64(char* buf, char* end, uint64_t num)
{
	return strint64(buf, end, 0, num);
}

#if BITS == 64

char* stri32(char* buf, char* end, int32_t num)
{
	return strint64(buf, end, num < 0, num < 0 ? -num : num);
}

char* stru32(char* buf, char* end, int32_t num)
{
	return strint64(buf, end, 0, num);
}

char* strli(char* buf, char* end, long num)
	__attribute__((alias("stri64")));
char* strul(char* buf, char* end, unsigned long num)
	__attribute__((alias("stru64")));

#endif
