#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <string.h>
#include <format.h>
#include <errtag.h>
#include <util.h>
#include <time.h>

ERRTAG("systime");
ERRLIST(NEBADF NEFAULT NEINVAL NEACCES NEFAULT NEINTR NELOOP NEMFILE
	NENFILE NENOENT NENOMEM NENOTDIR NEPERM NEROFS NETXTBSY NEWOULDBLOCK);

static const char rtc0[] = "/dev/rtc0";

#define RTC_RD_TIME	_IOR('p', 0x09, struct tm) /* Read RTC time   */
#define RTC_SET_TIME	_IOW('p', 0x0a, struct tm) /* Set RTC time    */

static void getsystime(struct timeval* tv, struct tm* tm)
{
	int ret;

	if((ret = sys_gettimeofday(tv, NULL)) < 0)
		fail("cannot get system time", NULL, ret);

	tv2tm(tv, tm);
}

static void setsystime(struct timeval* tv)
{
	int ret;

	if((ret = sys_settimeofday(tv, NULL)) < 0)
		fail("cannot set system time", NULL, ret);
}

static void getrtctime(struct timeval* tv, struct tm* tm, int rtcfd, const char* rtcname)
{
	int ret;

	memzero(tm, sizeof(*tm));

	if((ret = sys_ioctl(rtcfd, RTC_RD_TIME, tm)) < 0)
		fail("cannot get RTC time on", rtcname, ret);

	tm2tv(tm, tv);
}

static void setrtctime(struct tm* tm, int rtcfd, const char* rtcname)
{
	int ret;

	tm->isdst = 0;

	if((ret = sys_ioctl(rtcfd, RTC_SET_TIME, tm)) < 0)
		fail("cannot set RTC time on", rtcname, ret);
}

static char* parseymd(struct tm* tm, char* a)
{
	char* p = a; /* Expected format: 2015-12-14 */
	
	if(!(p = parseint(p, &tm->year)) || *p != '-')
		return NULL;
	if(!(p = parseint(p, &tm->mon)) || *p != '-')
		return NULL;
	if(!(p = parseint(p, &tm->mday)) || *p)
		return NULL;

	if(tm->mon > 12)
		return NULL;
	if(tm->mday > 31)
		return NULL;
	
	tm->mon--;
	tm->year -= 1900;

	return p;
}

static char* parsehms(struct tm* tm, char* a)
{
	char* p = a; /* Expected format: 20:40:17 */

	if(!(p = parseint(p, &tm->hour)) || *p != ':')
		return NULL;
	if(!(p = parseint(p, &tm->min)) || *p != ':')
		return NULL;
	if(!(p = parseint(p, &tm->sec)))
		return NULL;

	if(tm->hour >= 24)
		return NULL;
	if(tm->min >= 60)
		return NULL;
	if(tm->sec >= 60)
		return NULL;

	return p;
}

static void parsetime(struct timeval* tv, struct tm* tm, int argc, char** argv)
{
	char* p;

	if(argc > 2)
		fail("bad time specification", NULL, 0);
	if(argc == 1) {
		if(!(p = parselong(argv[0], &(tv->sec))) || *p)
			fail("not a timestamp:", argv[0], 0);
		tv2tm(tv, tm);
	} else {
		if(!(p = parseymd(tm, argv[0])) || *p)
			fail("cannot parse date (YYYY-MM-DD expected):", argv[0], 0);
		if(!(p = parsehms(tm, argv[0])) || *p)
			fail("cannot parse time (hh:mm:ss expected):", argv[1], 0);
		tm2tv(tm, tv);
	}
}

/* 0123456789 01234567 */
/* 2015-12-14 16:21:07 */

static void showtime(struct timeval* tv, struct tm* tm)
{
	char buf[50];
	char* end = buf + sizeof(buf) - 1;
	char* p = buf;

	p = fmtulong(p, end, tv->sec);
	p = fmtchar(p, end, ' ');
	p = fmttm(p, end, tm);
	*p++ = '\n';

	sys_write(1, buf, p - buf);
}

#define OPTS "rswd"
#define OPT_r (1<<0)
#define OPT_s (1<<1)
#define OPT_w (1<<2)
#define OPT_d (1<<3)

static int checkmode(int opts)
{
	int cnt = 0;

	if(opts & OPT_r) cnt++;
	if(opts & OPT_s) cnt++;
	if(opts & OPT_w) cnt++;

	return (cnt <= 1);
}

int main(int argc, char** argv)
{
	const char* rtcname = rtc0;
	struct tm tm;
	struct timeval tv;

	long rtcfd = -1;
	int opts = 0;
	int i = 1;

	/* Find out what we're about to do */
	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(!checkmode(opts))
		fail("cannot use more than one of -rsw", NULL, 0);

	/* Sort out rtc stuff, and open rtc if necessary */
	if(i < argc && (opts & OPT_d))
		rtcname = argv[i++];
	else if(opts & OPT_d)
		fail("RTC device name required", NULL, 0);
	if(opts & (OPT_w | OPT_r | OPT_s))
		if((rtcfd = sys_open(rtcname, O_RDONLY)) < 0)
			fail("cannot open", rtcname, rtcfd);

	/* Read time in, either by parsing arguments on via syscalls */
	if(i < argc && !(opts & (OPT_w | OPT_s)))
		fail("too many arguments", NULL, 0);
	if(i < argc && (opts & (OPT_w | OPT_s)))
		parsetime(&tv, &tm, argc - i, argv + i);
	else if(opts & (OPT_s | OPT_r))
		getrtctime(&tv, &tm, rtcfd, rtcname);
	else
		getsystime(&tv, &tm);

	/* Write time to rtc, sysclock or stdout */
	if(opts & OPT_w)
		setrtctime(&tm, rtcfd, rtcname);
	else if(opts & OPT_s)
		setsystime(&tv);
	else
		showtime(&tv, &tm);

	return 0;
}
