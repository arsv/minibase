#include <sys/open.h>
#include <sys/write.h>
#include <sys/ioctl.h>
#include <sys/gettimeofday.h>
#include <sys/settimeofday.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <time.h>
#include <fail.h>

ERRTAG = "systime";
ERRLIST = { 
	REPORT(EBADF), REPORT(EFAULT), REPORT(EINVAL), REPORT(EACCES),
	REPORT(EFAULT), REPORT(EINTR), REPORT(ELOOP), REPORT(EMFILE),
	REPORT(ENFILE), REPORT(ENOENT), REPORT(ENOMEM), REPORT(ENOTDIR),
	REPORT(EPERM), REPORT(EROFS), REPORT(ETXTBSY), REPORT(EWOULDBLOCK),
	RESTASNUMBERS 
};

static const char rtc0[] = "/dev/rtc0";

#define RTC_RD_TIME	_IOR('p', 0x09, struct tm) /* Read RTC time   */
#define RTC_SET_TIME	_IOW('p', 0x0a, struct tm) /* Set RTC time    */

static void getsystime(struct timeval* tv, struct tm* tm)
{
	xchk( sysgettimeofday(tv, NULL),
		"cannot get system time", NULL );
	tv2tm(tv, tm);
}

static void setsystime(struct timeval* tv)
{
	xchk( syssettimeofday(tv, NULL),
		"cannot set system time", NULL );
}

static void getrtctime(struct timeval* tv, struct tm* tm, int rtcfd, const char* rtcname)
{
	memset(tm, 0, sizeof(*tm));
	xchk( sysioctl(rtcfd, RTC_RD_TIME, (long) tm),
		"cannot get RTC time on", rtcname );
	tm2tv(tm, tv);
}

static void setrtctime(struct tm* tm, int rtcfd, const char* rtcname)
{
	tm->tm_isdst = 0;
	xchk( sysioctl(rtcfd, RTC_SET_TIME, (long) tm),
		"cannot set RTC time on", rtcname );
}

static char* parseymd(struct tm* tm, char* a)
{
	char* p = a; /* Expected format: 2015-12-14 */
	
	if(!(p = parseint(p, &tm->tm_year)) || *p != '-')
		return NULL;
	if(!(p = parseint(p, &tm->tm_mon)) || *p != '-')
		return NULL;
	if(!(p = parseint(p, &tm->tm_mday)) || *p)
		return NULL;

	if(tm->tm_mon > 12)
		return NULL;
	if(tm->tm_mday > 31)
		return NULL;
	
	tm->tm_mon--;
	tm->tm_year -= 1900;

	return p;
}

static char* parsehms(struct tm* tm, char* a)
{
	char* p = a; /* Expected format: 20:40:17 */

	if(!(p = parseint(p, &tm->tm_hour)) || *p != ':')
		return NULL;
	if(!(p = parseint(p, &tm->tm_min)) || *p != ':')
		return NULL;
	if(!(p = parseint(p, &tm->tm_sec)))
		return NULL;

	if(tm->tm_hour >= 24)
		return NULL;
	if(tm->tm_min >= 60)
		return NULL;
	if(tm->tm_sec >= 60)
		return NULL;

	return p;
}

static void parsetime(struct timeval* tv, struct tm* tm, int argc, char** argv)
{
	char* p;

	if(argc > 2)
		fail("bad time specification", NULL, 0);
	if(argc == 1) {
		if(!(p = parselong(argv[0], &(tv->tv_sec))) || *p)
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

	p = fmtulong(p, end, tv->tv_sec);
	p = fmtchar(p, end, ' ');
	p = fmttm(p, end, tm);
	*p++ = '\n';

	syswrite(1, buf, p - buf);
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
		rtcfd = xchk( sysopen(rtcname, O_RDONLY),
				"cannot open", rtcname );

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
