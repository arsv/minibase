#include <format.h>
#include <string.h>

char* fmtraw(char* p, char* e, void* data, int len)
{
	if(len > e - p)
		len = e - p;
	if(len <= 0)
		return p;

	memcpy(p, data, len);

	return p + len;
}
