#include <string.h>

size_t strlen(const char* p)
{
	size_t len = 0;

	while(*p++) len++;

	return len;
}
