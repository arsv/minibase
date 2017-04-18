#include <format.h>

char* parseoct(char* buf, int* np)
{
	int d, n = 0;
	char* p;

	for(p = buf; *p; p++)
		if(*p >= '0' && (d = *p - '0') < 8)
			n = n*8 + d;
		else
			break;

	*np = n;
	return p;
}
