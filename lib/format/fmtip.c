#include <format.h>

char* fmtip(char* p, char* e, uint8_t ip[4])
{
	p = fmtint(p, e, ip[0]);
	p = fmtchar(p, e, '.');
	p = fmtint(p, e, ip[1]);
	p = fmtchar(p, e, '.');
	p = fmtint(p, e, ip[2]);
	p = fmtchar(p, e, '.');
	p = fmtint(p, e, ip[3]);

	return p;
}
