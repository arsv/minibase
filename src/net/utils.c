#include <format.h>
#include <fail.h>

#include "utils.h"

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

char* parseipmask(char* p, uint8_t ip[5])
{
	int n;

	if(!(p = parseip(p, ip)))
		return p;

	if(*p == '/') {
		p = parseint(p+1, &n);
		if(n < 0 || n > 32) return NULL;
		ip[4] = n;
	} else {
		ip[4] = 32;
	};

	return p;
}

static int gotall(char* p)
{
	return (p && !*p);
}

void check_parse_ipmask(uint8_t ip[5], char* arg)
{
	if(!gotall(parseipmask(arg, ip)))
		fail("invalid address", arg, 0);
}

void check_parse_ipaddr(uint8_t ip[4], char* arg)
{
	if(!gotall(parseip(arg, ip)))
		fail("invalid address", arg, 0);
}

void need_one_more_arg(int i, int argc)
{
	if(i >= argc)
		fail("too few arguments", NULL, 0);
}

void need_no_more_args(int i, int argc)
{
	if(i < argc)
		fail("too many arguments", NULL, 0);
}
