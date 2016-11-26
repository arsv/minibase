#include <format.h>

const char* parseint(const char* buf, int* np)
{
	int d, n = 0;
	const char* p;

	for(p = buf; *p; p++)
		if(*p >= '0' && (d = *p - '0') < 10)
			n = n*10 + d;
		else
			break;

	*np = n;
	return p;
}
