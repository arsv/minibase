#include <format.h>

char* fmtchar(char* p, char* e, char c)
{
	if(p < e)
		*p++ = c;
	return p;
};
