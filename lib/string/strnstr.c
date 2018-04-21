#include <string.h>

char* strnstr(const char* str, const char* sub, size_t len)
{
	size_t sublen = strlen(sub);

	if(sublen > len)
		return NULL;

	const char* pos = str;
	const char* end = str + len - sublen;

	for(pos = str; pos <= end; pos++) {
		const char* p = pos;
		const char* q = sub;

		while(*q && *q == *p) {
			p++;
			q++;
		} if(!*q) {
			return (char*) pos;
		}
	}

	return NULL;
}
