#include <string.h>
#include <format.h>
#include <time.h>
#include <util.h>

/* struct tm { s, m, h, D, M, Y, wd, yd, dst } */
#define D(y, m, d) { 0, 0, 0, d, m - 1, y - 1900, 0, 0, 0 }

const struct test {
	struct tm tm;
	time_t ts;
} testdates[] = {
	{ D(1970,  1,  2),      86400 },
	{ D(1975,  5, 16),  169430400 },
	{ D(2015, 12, 31), 1451520000 },
	{ D(2016,  1,  1), 1451606400 },
	{ D(2016,  2, 29), 1456704000 },
	{ D(2017,  1,  5), 1483574400 },
	{ D(2020,  2, 28), 1582848000 },
	{ D(2025,  9, 30), 1759190400 }
};

static char* fmtts(char* p, char* e, time_t ts)
{
	return fmtpad(p, e, 10, fmtlong(p, e, ts));
}

static void check(const struct tm* tm, time_t exp, time_t got)
{
	if(got == exp)
		return;

	FMTBUF(p, e, buf, 100);
	p = fmttm(p, e, tm);
	p = fmtstr(p, e, " = ");
	p = fmtts(p, e, got);
	p = fmtstr(p, e, " != ");
	p = fmtts(p, e, exp);
	p = fmtstr(p, e, " FAIL");
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	_exit(0xFF);
}

int main(void)
{
	int ntests = sizeof(testdates)/sizeof(*testdates);
	const struct test* buf = testdates;
	const struct test* end = testdates + ntests;
	const struct test* tst;

	for(tst = buf; tst < end; tst++) {
		struct timeval tv;

		tm2tv(&tst->tm, &tv);

		check(&tst->tm, tst->ts, tv.sec);
	}

	return 0;
}
