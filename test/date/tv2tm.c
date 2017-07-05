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

static int check(time_t ts, const struct tm* exp, struct tm* got)
{
	char buf[500];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;
	int ret;

	p = fmtpad(p, e, 10, fmtlong(p, e, ts));
	p = fmtstr(p, e, " = ");
	p = fmttm(p, e, got);

	if(!memcmp(exp, got, sizeof(*exp))) {
		p = fmtstr(p, e, " OK");
		ret = 0;
	} else {
		p = fmtstr(p, e, " / ");
		p = fmttm(p, e, exp);
		p = fmtstr(p, e, " FAIL");
		ret = !0;
	};

	*p++ = '\n';

	writeall(STDERR, buf, p - buf);

	return ret;
}

int main(void)
{
	int ret = 0;
	int ntests = sizeof(testdates)/sizeof(*testdates);
	const struct test* buf = testdates;
	const struct test* end = testdates + ntests;
	const struct test* tst;

	for(tst = buf; tst < end; tst++) {
		struct tm tm;
		struct timeval tv = { tst->ts, 0 };

		memzero(&tm, sizeof(tm));

		tv2tm(&tv, &tm);

		/* blank out fields we do not check */
		tm.wday = 0;
		tm.yday = 0;

		ret |= check(tst->ts, &tst->tm, &tm);
	}

	return ret;
}
