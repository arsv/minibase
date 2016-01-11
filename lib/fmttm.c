#include <bits/time.h>

#include "fmtulp.h"
#include "fmtchar.h"
#include "fmttm.h"

char* fmttm(char* buf, char* end, struct tm* tm)
{
	char* p = buf;

	p = fmtulp(p, end, tm->tm_year + 1900, 4);
	p = fmtchar(p, end, '-');
	p = fmtulp(p, end, tm->tm_mon + 1, 2);
	p = fmtchar(p, end, '-');
	p = fmtulp(p, end, tm->tm_mday, 2);
	p = fmtchar(p, end, ' ');

	p = fmtulp(p, end, tm->tm_hour, 2);
	p = fmtchar(p, end, ':');
	p = fmtulp(p, end, tm->tm_min, 2);
	p = fmtchar(p, end, ':');
	p = fmtulp(p, end, tm->tm_sec, 2);

	return p;
}
