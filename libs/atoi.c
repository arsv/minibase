#include "atoi.h"

int atoi(const char* a)
{
	int d, n = 0;
	int min = 0;

	if(*a == '-') {
		a++;
		min = 1;
	};

	for(; *a; a++)
		if(*a >= '0' && ((d = *a - '0') < 10))
			n = (n*10) + d;
		else
			break;

	return min ? -n : n;
}
