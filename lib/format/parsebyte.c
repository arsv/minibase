#include <bits/null.h>
#include <format.h>

static int hexdigit(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	else if(c >= 'a' && c <= 'f')
		return c - 'a' + 0x0A;
	else if(c >= 'A' && c <= 'F')
		return c - 'A' + 0x0A;
	else
		return -1;
}

char* parsebyte(char* p, uint8_t* v)
{
	int q;
	int r;

	if((q = hexdigit(*p)) < 0)
		return NULL;

	r = q << 4; p++;

	if((q = hexdigit(*p)) < 0)
		return NULL;

	*v = r | q; p++;

	return p;
}
