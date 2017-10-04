#include <format.h>

char* fmthex(char* buf, char* end, unsigned num)
{
	static const char digits[] = "0123456789ABCDEF";

	int len = 0;
	long n;

	for(len = 1, n = num; n >= 0x10; len++)
		n >>= 4;

	int i;
	char* e = buf + len;
	char* p = e - 1; /* len >= 1 so e > buf */
	
	for(i = 0; i < len; i++, p--, num >>= 4)
		if(p < end)
			*p = digits[num & 0x0F];

	return e; 
}
