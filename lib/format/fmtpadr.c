#include <format.h>

char* fmtpadr(char* p, char* e, int width, char* q)
{
	if(q < p || q > e)
		return q;

	char* t = p + width;

	while(q < t && q < e)
		*q++ = ' ';

	return q;
}
