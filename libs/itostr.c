#include "itostr.h"

char* itostr(char* buf, char* end, int num)
{
	char intbuf[8*sizeof(int)/3+2];
	char* p = intbuf;
	int i, l, n;

	if(num < 0) { num = -num; *p++ = '-'; }

	for(l = 1, n = num; n; l++)
		n /= 10;

	for(i = 0, n = num; i < l; i++, n /= 10)
		if(p + i < end)
			p[i] = '0' + n % 10;

	return (p + l < end ? p + l : end);
}
