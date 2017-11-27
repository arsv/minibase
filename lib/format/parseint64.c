#include <bits/ints.h>
#include <format.h>

char* parseu64(char* buf, uint64_t* np)
{
	uint64_t n = 0;
	int d;
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
