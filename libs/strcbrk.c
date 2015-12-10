#include "strcbrk.h"

char* strcbrk(char* str, char c)
{
	char* p;

	for(p = str; *p && *p != c; p++)
		;
	
	return p;
}
