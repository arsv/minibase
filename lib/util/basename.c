#include <util.h>

char* basename(char* path)
{
	char* p = path;
	char* q = path;

	while(*p) if(*p++ == '/') q = p;

	return q;
}
