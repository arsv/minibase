#include <cdefs.h>
#include <format.h>

char* parseip(char* p, uint8_t ip[4])
{
	int i, n;

	for(i = 0; i < 4; i++) {
		if(i && *p != '.')
			return NULL;
		if(i) p++;

		if(!(p = parseint(p, &n)))
			return NULL;
		if(n < 0 || n > 255)
			return NULL;

		ip[i] = n;
	}

	return p;
}

