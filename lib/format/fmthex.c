#include <format.h>

char* fmthex(char* p, char* e, unsigned n)
{
	static const char digits[] = "0123456789ABCDEF";

	while(p < e && n) {
		*p++ = digits[n % 16];
		n /= 16;
	}

	return p;
}
