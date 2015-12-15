#include <fail.h>
#include <null.h>
#include "argbits.h"

int argbits(const char* key, const char* arg)
{
	char opt[] = "-?";
	const char* a;
	const char* k;
	int ret = 0;

	for(a = arg; *a; a++) {
		for(k = key; *k; k++)
			if(*a == *k)
				break;
		if(*k) {
			ret |= (1 << (k - key));
		} else {
			opt[1] = *a;
			fail("unknown option", opt, 0);
		}
	}

	return ret;
}
