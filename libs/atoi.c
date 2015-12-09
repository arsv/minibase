#include "atoi.h"

int atoi(const char* a)
{
	int d, n = 0;
	const char* p;

	for(p = a; *p; p++)
		if(*p >= '0' && ((d = *p - '0') < 10))
			n = (n*10) + d;
		else
			break;

	return n;
}
