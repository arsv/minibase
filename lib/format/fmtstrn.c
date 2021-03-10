#include <format.h>
#include <string.h>
#include <util.h>

char* fmtstrn(char* p, char* e, const char* src, int len)
{
	return fmtraw(p, e, src, strnlen(src, len));
}
