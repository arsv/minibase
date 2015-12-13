#include "xatol.h"
#include "fail.h"

long xatol(const char* a)
{
	int d, m, n = 0;
	int min = 0;
	const char* orig = a;

	if(*a == '-') {
		a++;
		min = 1;
	};

	for(; *a; a++) {
		if(*a >= '0' && ((d = *a - '0') < 10))
			m = (n*10) + d;
		else
			break;
		if(m > n)
			n = m;
		else
			break;
	} if(*a)
		fail("invalid number", orig, 0);

	return min ? -n : n;
}
