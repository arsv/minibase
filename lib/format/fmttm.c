#include <bits/time.h>
#include <format.h>

char* fmttm(char* p, char* e, const struct tm* tm)
{
	p = fmtulp(p, e, tm->year + 1900, 4);
	p = fmtchar(p, e, '-');
	p = fmtulp(p, e, tm->mon + 1, 2);
	p = fmtchar(p, e, '-');
	p = fmtulp(p, e, tm->mday, 2);
	p = fmtchar(p, e, ' ');

	p = fmtulp(p, e, tm->hour, 2);
	p = fmtchar(p, e, ':');
	p = fmtulp(p, e, tm->min, 2);
	p = fmtchar(p, e, ':');
	p = fmtulp(p, e, tm->sec, 2);

	return p;
}
