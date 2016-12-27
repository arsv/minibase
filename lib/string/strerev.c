#include <string.h>

char* strerev(char* p, char* e, char c)
{
	char* z = e;

	while(z > p && *(z-1) != c) z--;

	return z;
}

