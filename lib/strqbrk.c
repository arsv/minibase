/* Like strpbrk but never returns NULL */
#include "strqbrk.h"

char* strqbrk(char* s, const char *accept)
{
	const char* a;

	for(; *s; s++)
		for(a = accept; *a; a++)
			if(*s == *a)
				return s;

	return s;
}
