#include <string.h>

/* Like strpbrk(3) but with a single delimiter character. */

char* strcbrk(char* str, char c)
{
	char* p;

	for(p = str; *p && *p != c; p++)
		;
	
	return p;
}
