#include <sys/clock_gettime.h>
#include <sys/gettimeofday.h>

#include <format.h>
#include <util.h>
#include <fail.h>
#include <time.h>

ERRTAG = "uptime";
ERRLIST = {
	REPORT(EINVAL), REPORT(ENOSYS), REPORT(EPIPE),
	REPORT(EBADFD), RESTASNUMBERS
};

static long gettime(int clkid)
{
	struct timespec ts;
	xchk(sysclock_gettime(clkid, &ts), "cannot get system time", NULL);
	return ts.tv_sec;
}

static void writeline(char* buf, char* end)
{
	*end++ = '\n';
	xchk(writeall(1, buf, end - buf), "write", NULL);
}

static char* fmtpart(char* p, char* end, long n, char* unit)
{
	if(!n) goto out;
	p = fmtstr(p, end, " ");
	p = fmtlong(p, end, n);
	p = fmtstr(p, end, " ");
	p = fmtstr(p, end, unit);
	if(n <= 1) goto out;
	p = fmtstr(p, end, "s");
out:	return p;
}

static void showtime(int ts, char* note)
{
	char buf[1024];
	char* end = buf + sizeof(buf) - 1;
	char* p = buf;

	p = fmtlong(p, end, ts);
	p = fmtstr(p, end, " ");

	int sec = ts % 60; ts /= 60;
	int min = ts % 60; ts /= 60;
	int hrs = ts % 24; ts /= 24;

	p = fmtpart(p, end, ts, "day");
	p = fmtpart(p, end, hrs, "hour");
	p = fmtpart(p, end, min, "minute");
	p = fmtpart(p, end, sec, "second");

	if(note) p = fmtstr(p, end, note);

	writeline(buf, p);
}

static void uptime(int clkid, char* note)
{
	showtime(gettime(clkid), note);
}

static void showsince(struct timeval* tv, struct tm* tm)
{
	char buf[100];
	char* end = buf + sizeof(buf) - 1;
	char* p = buf;

	p = fmtulong(p, end, tv->tv_sec);
	p = fmtchar(p, end, ' ');
	p = fmttm(p, end, tm);

	writeline(buf, p);
}

static void upsince(void)
{
	long uptime = gettime(CLOCK_BOOTTIME);
	struct timeval tv;
	struct tm tm;

	xchk(sysgettimeofday(&tv, NULL), "cannot get system time", NULL);
	tv.tv_sec -= uptime;

	tv2tm(&tv, &tm);
	showsince(&tv, &tm);
}

#define OPTS "sw"
#define OPT_s (1<<0)	/* up-since */
#define OPT_w (1<<1)	/* time awake */

int main(int argc, char** argv)
{
	int opts = 0;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc)
		fail("too many arguments", NULL, 0);
	if((opts & (OPT_s | OPT_w)) == (OPT_s | OPT_w))
		fail("cannot use -b and -w at the same time", NULL, 0);

	if(opts & OPT_s)
		upsince();
	else if(opts & OPT_w)
		uptime(CLOCK_MONOTONIC, " awake");
	else
		uptime(CLOCK_BOOTTIME, NULL);

	return 0;
}
