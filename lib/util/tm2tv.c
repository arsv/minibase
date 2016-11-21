#include <bits/time.h>
#include <time.h>

/* This is an adaptation of mktime64 from linux/kernel/time/time.c
   that works on *tm and *tv.
 
   struct tm is assumed to be in common userspace format, i.e. year=115
   for 2015 and mon=0 for January. Days are 1-based. DST is ignored. */

void tm2tv(struct tm* tm, struct timeval* tv)
{
	unsigned int day = tm->tm_mday;
	unsigned int mon = tm->tm_mon + 1;
	unsigned int year = tm->tm_year + 1900;

	/* 1..12 -> 11,12,1..10 */
	if (0 >= (int) (mon -= 2)) {
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}

	uint64_t ts;
	
	ts = (year/4 - year/100 + year/400 + 367*mon/12 + day);
	ts += year*365 - 719499;	/* days */

	ts = ts*24 + tm->tm_hour;	/* now have hours */
	ts = ts*60 + tm->tm_min;	/* now have minutes */
	ts = ts*60 + tm->tm_sec;	/* finally seconds */

	tv->tv_sec = ts;
	tv->tv_usec = 0;
}
