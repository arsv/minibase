#include <string.h>
#include <format.h>

char* fmtstr(char* p, char* e, const char* str)
{
	if(p >= e) return p;

	size_t n = e - p;
	size_t len = strnlen(str, n);

	return fmtraw(p, e, str, len);
}
