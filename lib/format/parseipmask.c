#include <cdefs.h>
#include <format.h>

char* parseipmask(char* p, uint8_t* ip, uint8_t* mask)
{
	int n;

	if(!(p = parseip(p, ip)))
		return p;

	if(*p == '/') {
		p = parseint(p+1, &n);

		if(n < 0 || n > 32)
			return NULL;

		*mask = n;
	} else {
		*mask = 32;
	};

	return p;
}
