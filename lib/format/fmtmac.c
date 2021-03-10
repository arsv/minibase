#include <format.h>

char* fmtmac(char* p, char* e, uint8_t mac[6])
{
	int i;

	p = fmtbyte(p, e, mac[0]);

	for(i = 1; i < 6; i++) {
		p = fmtchar(p, e, ':');
		p = fmtbyte(p, e, mac[i]);
	};

	return p;
}

