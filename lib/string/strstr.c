#include <string.h>

char* strstr(const char* str, const char* sub)
{
	const char* p = str;
	const char* q = str;
	const char* r;

	for(p = str; *p; p++) {
		for(r = p, q = sub; *q && *q == *r; r++, q++)
			;
		if(!*q)
			return (char*) p;
	}

	return NULL;
}
