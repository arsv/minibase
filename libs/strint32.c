#include <bits/ints.h>
#include "strint32.h"

#if BITS == 32

static char* strint32(char* buf, char* end, int minus, uint32_t num)
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

	return e; 
}

char* stri32(char* buf, char* end, int32_t num)
{
	return strint32(buf, end, num < 0, num < 0 ? -num : num);
}

char* stru32(char* buf, char* end, uint32_t num)
{
	return strint32(buf, end, 0, num);
}

char* strli(char* buf, char* end, long num)
	__attribute__((alias("stri32")));
char* strul(char* buf, char* end, unsigned long num)
	__attribute__((alias("stru32")));

#endif
