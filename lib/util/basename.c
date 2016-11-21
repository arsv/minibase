#include <util.h>

const char* basename(const char* path)
{
	const char* p = path;
	const char* q = path;

	while(*p) if(*p++ == '/') q = p;

	return q;
}
