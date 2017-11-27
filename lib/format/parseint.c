#include <format.h>

char* parseint(char* buf, int* np)
{
	int d, n = 0;
	char* p;

	for(p = buf; *p; p++)
		if(*p >= '0' && (d = *p - '0') < 10)
			n = n*10 + d;
		else
			break;

	if(p == buf)
		return NULL;

	*np = n;
	return p;
}
