/* Local time handling is all screwed up. POSIX traditions
   and tzdata formats are not helping at all.

   The goals for this tool:
     1. convert system time or arbitrary unix timestamp to local time zone
         "1479823945" -> "2016-11-22 16:12:25 UTC+2"
     2a. convert time given w/ explicit offset to local time zone
         "2016-11-20 11:10 UTC-5" -> "2016-11-20 18:10 UTC+2"
     2b. same, with non-ambiguous "standard" offset abbreviations:
         "2016-11-20 11:10 EST" eq UTC-5
     2c. same, with ambiguous offset abbreviations:
         "2016-11-20 11:10 ET" eq EST eq UTC-5
     3. convert any of above to a given UTC offset

   There is some ambiguity as to whether the system clock runs in UTC,
   TAI, or TAI w/ vaguely-defined initial offset. This tool should work
   well in either case as long as the zonefile correctly specifies
   system-to-UTC mapping. */

#include <sys/time.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <time.h>
#include <util.h>
#include <main.h>

#include "date.h"

ERRTAG("date");

#define OPTS "uftdq"
#define OPT_u (1<<0)
#define OPT_f (1<<1)
#define OPT_t (1<<2)
#define OPT_d (1<<3)
#define OPT_q (1<<4)

static int lookslikezone(const char* arg)
{
	if(*arg >= '0' && *arg <= '9')
		return 0;
	return 1;
}

static int lookslikestamp(const char* arg)
{
	const char* c;

	if(!*arg) return 0;

	for(c = arg; *c; c++)
		if(*c < '0' || *c > '9')
			return 0;

	return 1;
}

/* Time is [0-9:]+ and date is [0-9-]+. This is enough to prevent
   confusion as to what is where. Actual validity will be checked
   later anyway by parse*. */

static int only_digits_and(char what, char* arg)
{
	char* p;

	for(p = arg; *p; p++)
		if(*p >= '0' && *p <= '9')
			continue;
		else if(*p == what)
			continue;
		else
			return 0;
	if(p == arg)
		return 0;

	return 1;
}

static int looksliketime(char* arg)
{
	return only_digits_and(':', arg);
}

static int lookslikedate(char* arg)
{
	return only_digits_and('-', arg);
}

static void systemtime(struct timedesc* zt)
{
	int ret;

	if((ret = sys_gettimeofday(&zt->tv, NULL)) < 0)
		fail("gettimeofday", NULL, ret);

	zt->type = TIME_TV;
}

static void decode_stamp(struct timedesc* zt, char* arg)
{
	long ts;
	char* p;

	if(!(p = parselong(arg, &ts)) || *p)
		fail("cannot parse timestamp:", arg, 0);

	zt->tv.sec = ts;
	zt->tv.usec = 0;
	zt->type = TIME_TV;

	tv2tm(&zt->tv, &zt->tm);
}

static void decode_ymd(struct timedesc* zt, const char* arg)
{
	char* p = (char*) arg;
	int y, m, d;

	if(!(p = parseint(p, &y))) goto bad;
	if(*p++ != '-') goto bad;
	if(!(p = parseint(p, &m))) goto bad;
	if(*p++ != '-') goto bad;
	if(!(p = parseint(p, &d))) goto bad;
	if(*p) goto bad;

	zt->tm.year = y - 1900;
	zt->tm.mon = m;
	zt->tm.mday = d;

	return;
bad:
	fail("cannot parse date:", arg, 0);
}

static void decode_hms(struct timedesc* zt, const char* arg)
{
	char* p = (char*) arg;
	int h, m, s;

	if(!(p = parseint(p, &h))) goto bad;
	if(*p++ != ':') goto bad;
	if(!(p = parseint(p, &m))) goto bad;

	if(!*p)
		s = 0;
	else if(*p != ':')
		goto bad;
	else if(!(p = parseint(p, &s)))
		goto bad;
	if(*p)
		goto bad;

	zt->tm.hour = h;
	zt->tm.min = m;
	zt->tm.sec = s;

	return;
bad:
	fail("cannot parse time:", arg, 0);
}

static void decode_time(struct timedesc* zt, int argc, char** argv)
{
	zt->zone = NULL;

	if(argc <= 0)
		return systemtime(zt);
	if(argc == 1 && lookslikestamp(argv[0]))
		return decode_stamp(zt, argv[0]);

	zt->tv.sec = 0;
	zt->tv.usec = 0;

	if(argc > 1 && lookslikezone(argv[argc-1]))
		zt->zone = argv[--argc];
	if(argc > 2)
		fail("too many arguments", NULL, 0);

	if(argc == 2) {   /* 2016-11-20 10:00 PDT */
		decode_ymd(zt, argv[0]);
		decode_hms(zt, argv[1]);
		zt->type = TIME_TM_ALL;
	} else {
		systemtime(zt); /* we'll need tv later */

		if(looksliketime(argv[0]))        /* 10:00 PDT */
			decode_hms(zt, argv[0]);
		else if(lookslikedate(argv[0]))   /* 2016-11-20 PDT */
			fail("time must be supplied in this mode", NULL, 0);
		else
			fail("unexpected time format", NULL, 0);
		zt->type = TIME_TM_HMS;
	}
}

static char* fmtint0(char* p, char* end, int n, int w)
{
	return fmtpad0(p, end, w, fmti32(p, end, n));
}

static char* fmtzone(char* p, char* end, int diff)
{
	p = fmtstr(p, end, "UTC");

	if(!diff) return p;

	p = fmtchar(p, end, diff > 0 ? '+' : '-');
	if(diff < 0) diff = -diff;
	diff /= 60;
	p = fmtint0(p, end, diff / 60, 2);
	diff %= 60;

	if(!diff) return p;

	p = fmtchar(p, end, ':');
	p = fmtint0(p, end, diff, 2);

	return p;
}

static const char wdays[][3] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static char* fmtwday(char* p, char* end, int wday)
{
	if(wday < 0 || wday > 6)
		return fmtlong(p, end, wday);
	return fmtstrn(p, end, wdays[wday], 3);
}

static void show_time(struct timedesc* zt, const char* format)
{
	const char* c;
	struct tm* tm = &zt->tm;

	FMTBUF(p, e, buf, 200);

	for(c = format; *c; c++) switch(*c) {
		case 'Y': p = fmtint0(p, e, 1900 + tm->year, 4); break;
		case 'M': p = fmtint0(p, e, tm->mon+1, 2); break;
		case 'D': p = fmtint0(p, e, tm->mday, 2); break;
		case 'h': p = fmtint0(p, e, tm->hour, 2); break;
		case 'm': p = fmtint0(p, e, tm->min, 2); break;
		case 's': p = fmtint0(p, e, tm->sec, 2); break;
		case 'w': p = fmtwday(p, e, tm->wday); break;
		case 'u': p = fmtlong(p, e, zt->tv.sec); break;
		case 'z': p = fmtzone(p, e, zt->offset); break;
		default: p = fmtchar(p, e, *c);
	}

	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static const char* chooseformat(const char* format, int opts)
{
	int mode = opts;

	if(mode == OPT_f)
		return format;
	if(mode == OPT_q)
		return "h:m";
	if(mode == OPT_t)
		return "h:m:s";
	if(mode == OPT_d)
		return "Y-M-D";
	if(mode == (OPT_d | OPT_t))
		return "Y-M-D h:m:s";
	if(mode == (OPT_d | OPT_q))
		return "Y-M-D h:m";
	if(mode == OPT_u)
		return "u";
	if(mode)
		fail("incorrect use of -dtuqf", NULL, 0);

	return "w Y-M-D h:m:s z";
}

int main(int argc, char** argv)
{
	char** envp = argv + argc + 1;
	int i = 1, opts = 0;
	struct timedesc zt;
	const char* format;
	const char* zone;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(i < argc && (opts & OPT_f))
		format = argv[i++];
	else if(opts & OPT_f)
		fail("argument required for -f", NULL, 0);
	else
		format = NULL;

	if(i < argc && lookslikezone(argv[i]))
		zone = argv[i++];
	else
		zone = getenv(envp, "TZ");

	decode_time(&zt, argc - i, argv + i);

	apply_zones(&zt, zone);

	format = chooseformat(format, opts);

	show_time(&zt, format);

	return 0;
}
