#include <bits/time.h>

/* Inverse of tv2tm(). Sometimes called gmtime_r but that's
   not exactly true, so let's not add to confusion.
   Musl calls it __secs_to_tm() internally.  */

void tv2tm(struct timeval* tv, struct tm* tm)
{
	uint64_t ts = tv->tv_sec;	
	static const char mdays[] = {31,30,31,30,31,31,30,31,30,31,31,29};

	tm->tm_sec = ts % 60; ts /= 60;
	tm->tm_min = ts % 60; ts /= 60;
	tm->tm_hour = ts % 24; ts /= 24;
	/* ts is days now */
	ts += 719499;
	/* ts is (year/4 - year/100 + year/400 + 367*mon/12 + day + 365*year) */

	/* two iterations to account for leap days */
	int year1 = ts / 365;
	int diff1 = year1/4 - year1/100 + year1/400;

	int year2 = (ts - diff1) / 365;
	int diff2 = year2/4 - year2/100 + year2/400;

	int year = (ts - diff2) / 365;
	int yday = ts - year2/4 - year2/100 + year2/400 - 365*year;

	int mon, mday, next, dsum = 0;
	for(mon = 0; mon < 12; mon++)
		if((next = dsum + mdays[mon]) > yday)
			break;
		else
			dsum = next;

	/* Reverse Feb-last-month logic above; mon is 0-based */
	mday = yday - dsum; mon += 2;
	if(mon > 11) { mon -= 12; year += 1; }

	tm->tm_mday = mday;
	tm->tm_mon = mon;
	tm->tm_year = year - 1900;
}
