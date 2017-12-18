#include <format.h>

char* parsexlong(char* buf, ulong* np)
{
	int d;
	ulong n = 0;
	char* p;

	for(p = buf; *p; p++)
		if(*p >= '0' && (d = *p - '0') < 10)
			n = n*16 + d;
		else if(*p >= 'a' && (d = *p - 'a') < 6)
			n = n*16 + 0xA + d;
		else if(*p >= 'A' && (d = *p - 'A') < 6)
			n = n*16 + 0xA + d;
		else
			break;

	if(p == buf)
		return NULL;

	*np = n;
	return p;
}
