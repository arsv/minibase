#include <bits/time.h>
#include <time.h>

/* Inverse of tv2tm(). Sometimes called gmtime_r but that's
   not exactly true, so let's not add to confusion.
   Musl calls it __secs_to_tm() internally.  */

/* Code taken from musl. My oh-so-clever version naturally failed badly
   on Feb 1, 2016 (leap year). Big surprise, yeah. */

/* 2000-03-01 (mod 400 year, immediately after feb29 */
#define LEAPOCH (946684800LL + 86400*(31+29))

#define DAYS_PER_400Y (365*400 + 97)
#define DAYS_PER_100Y (365*100 + 24)
#define DAYS_PER_4Y   (365*4   + 1)

void tv2tm(const struct timeval* tv, struct tm* tm)
{
	time_t ts = tv->sec;	
	static const char mdays[] = {31,30,31,30,31,31,30,31,30,31,31,29};

	ts -= LEAPOCH;
	int days = ts / 86400;
	int secs = ts % 86400;

	/* with LEAPOCH, ts may happen to be negative */
	if(secs < 0) { secs += 86400; days--; };

	int wday = (3+days) % 7;
	if(wday < 0) wday += 7;

	int qc = days / DAYS_PER_400Y;
	days = days % DAYS_PER_400Y;
	if(days < 0) { days += DAYS_PER_400Y; qc--; };

	int nc = days / DAYS_PER_100Y; if(nc == 4) nc--;
	days -= nc*DAYS_PER_100Y;

	int nq = days / DAYS_PER_4Y; if(nq == 25) nq--;
	days -= nq*DAYS_PER_4Y;

	int year = days / 365; if(year == 4) year--;
	days -= year*365;

	int leap = !year && (nq || !nc);
	int yday = days + 31 + 28 + leap;
	if (yday >= 365+leap) yday -= 365+leap;

	year += 4*nq + 100*nc + 400*qc;

	int mon = 0;
	for(; mon < 12 && mdays[mon] <= days; mon++)
		days -= mdays[mon];

	tm->year = year + 100;
	tm->mon = mon + 2;
	if (tm->mon >= 12) {
		tm->mon -=12;
		tm->year++;
	}
	tm->mday = days + 1;
	tm->wday = wday;
	tm->yday = yday;

	tm->sec = secs % 60; secs /= 60;
	tm->min = secs % 60; secs /= 60;
	tm->hour = secs % 24;

	tm->isdst = 0;
}
