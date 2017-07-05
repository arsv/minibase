#include <time.h>
#include <string.h>
#include <format.h>

/* struct tm { s, m, h, D, M, Y, wd, yd, dst } */
#define D(y, m, d, ts) { { 0, 0, 0, d, m - 1, y - 1900, 0, 0, 0 }, ts }

struct test {
	struct tm tm;
	time_t ts;
} testdates[] = {
	D(1970,  1,  2,      86400),
	D(1975,  5, 16,  169430400),
	D(2015, 12, 31, 1451520000),
	D(2016,  1,  1, 1451606400),
	D(2016,  2, 29, 1456704000),
	D(2017,  1,  5, 1483574400),
	D(2020,  2, 28, 1582848000),
	D(2025,  9, 30, 1759190400)
};

int main(void)
{
	int ret = 0;
	struct test* buf = testdates;
	struct test* end = testdates + sizeof(testdates)/sizeof(*testdates);
	struct test* tst;

	for(tst = buf; tst < end; tst++) {
		struct timeval tv;

		tm2tv(&tst->tm, &tv);

		int year = 1900 + tst->tm.year;
		int mon = 1 + tst->tm.mon;
		int day = tst->tm.mday;
		int hour = tst->tm.hour;
		int min = tst->tm.min;
		int sec = tst->tm.sec;

		time_t tsref = tst->ts;
		time_t tsnew = tv.sec;

		if(tsref == tsnew) {
			eprintf("%04i-%02i-%02i %02i:%02i:%02i = %li OK\n",
					year, mon, day, hour, min, sec, tsnew);
		} else {
			eprintf("%04i-%02i-%02i %02i:%02i:%02i = %li != %li FAIL d=%i\n",
					year, mon, day, hour, min, sec, tsnew,
					tsref, tsnew - tsref);
			ret = -1;
		}
	}

	return ret;
}
