#include <bits/time.h>
#include <format.h>

char* fmttm(char* buf, char* end, struct tm* tm)
{
	char* p = buf;

	p = fmtulp(p, end, tm->year + 1900, 4);
	p = fmtchar(p, end, '-');
	p = fmtulp(p, end, tm->mon + 1, 2);
	p = fmtchar(p, end, '-');
	p = fmtulp(p, end, tm->mday, 2);
	p = fmtchar(p, end, ' ');

	p = fmtulp(p, end, tm->hour, 2);
	p = fmtchar(p, end, ':');
	p = fmtulp(p, end, tm->min, 2);
	p = fmtchar(p, end, ':');
	p = fmtulp(p, end, tm->sec, 2);

	return p;
}
