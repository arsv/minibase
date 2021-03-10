#include <string.h>

int strncmp(const char* a, const char* b, size_t n)
{
	size_t bn = strnlen(b, n + 1);
	size_t an = strnlen(a, n);
	int dn = an - bn;

	return dn ? dn : memcmp(a, b, an);
}
