#include "itostr.h"
#include "strapp.h"

char* itostr(char* buf, char* end, int num)
{
	int len = 0;
	int min = 0;

	if(num < 0) { num = -num; min = 1; }

	int n;

	for(len = 1, n = num; n >= 10; len++)
		n /= 10;

	int i;
	char* e = buf + len + min;
	char* p = e - 1; /* len >= 1 so e > buf */
	
	for(i = 0; i < len; i++, p--, num /= 10)
		if(p < end)
			*p = '0' + num % 10;
	if(min) *p = '-';

	return e; 
}
