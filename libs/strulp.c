#include "strulp.h"

/* Could have merged it with stri{32,64}, but the problem is that
   padding only makes sense on unsigned integers, and it's not likely
   that we will ever pad 64-bit integers on 32-bit arches.

   So, let's keep it a separate function with a arch-neutral long arg. */

char* strulp(char* buf, char* end, unsigned long num, int pad)
{
	int len;
	unsigned long n;

	for(len = 1, n = num; n >= 10; len++)
		n /= 10;
	if(len < pad)
		len = pad;

	int i;
	char* e = buf + len;
	char* p = e - 1; /* len >= 1 so e > buf */
	
	for(i = 0; i < len; i++, p--, num /= 10)
		if(p < end)
			*p = '0' + num % 10;

	return e; 
}
