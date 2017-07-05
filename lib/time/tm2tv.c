#include <bits/time.h>
#include <time.h>

/* struct tm is assumed to be in common userspace format, i.e. year=115
   for 2015 and mon=0 for January. Days are 1-based. DST is ignored. */

static const short mdays[] = {
	31, 28, 31, 30,
	31, 30, 31, 31,
	30, 31, 30, 31
};

void tm2tv(const struct tm* tm, struct timeval* tv)
{
	int day = tm->mday; /* 1-based */
	int mon = tm->mon;  /* 0-based */
	int year = tm->year + 1900;

	time_t ts = (year - 1970)*365; /* days */

	if(mon < 2) year--; /* do not count possible Feb 29 this year */

	int leaps_to_1970 = 1970/4 - 1970/100 + 1970/400;
	int leaps_to_year = year/4 - year/100 + year/400;

	ts += leaps_to_year - leaps_to_1970; /* still days */

	for(int i = 0; i < mon; i++)
		ts += mdays[i];

	ts += (day - 1); /* account for day being 1-based */

	ts = ts*24 + tm->hour; /* hours */
	ts = ts*60 + tm->min;  /* minutes */
	ts = ts*60 + tm->sec;  /* seconds */

	tv->sec = ts;
	tv->usec = 0;
}
