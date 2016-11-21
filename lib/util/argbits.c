#include <util.h>
#include <fail.h>

/* A lot of tool are ok with first-argument-only, squashed options, as in

	mount -re blah ...

   which can not be replaced by

	mount -r -e blah ...

   The backing assuption is that the options never take arguments of its own.
   As make shows (see its code), the tool still can accept different arguments
   _depending_ on the options given.

   Allowing -opt stuff as the first argument only also simplifies subsequent
   argument quoting; it is ok to pass - as that first arguments and the rest
   won't be checked for leading - anyway.

   Key here is a string like "abc". Given -ac for arg, the function returns
   an int with bits 0 (index of a in key) and 2 (index of c) set. */

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
