#include <string.h>

char* strnstr(const char* str, const char* sub, size_t len)
{
	size_t sublen = strlen(sub);

	const char* p = str;
	const char* q = str;
	const char* r;

	const char* end = str + len - sublen;

	if(end <= str)
		return NULL;

	for(p = str; p < end; p++) {
		for(r = p, q = sub; *q && *q == *r; r++, q++)
			;
		if(!*q)
			return (char*) p;
	}

	return NULL;
}
