#include <string.h>
#include <time.h>
#include <fail.h>
#include "date.h"

/* So here we've got user-supplied time description in zt,
   and the zone to convert it to. When this module returns,
   zt must contain unix time in tv, local time in tm, and
   proper UTC offset.

   The translation is based on the following trick:

       localtime(tv) == tv2tm(tv + shift) + leapsec?

   The mapping tv2tm implements is a kind of perfect Gregorian
   calendar, and zone offsets are accounted for by shifting its
   input.

   Since tv2tm never generates leap seconds (i.e. tm_sec = 60),
   the leaps must be accounted for after tv2tm has been applied.

   Note that gmtime(tv) != tv2tm(tv). UTC aka gmtime is just a zone
   with 0 UTC shift; leap seconds still apply.

   We assume that leap seconds are correctly accounted for,
   in the same way, in *any* zone file. */

#if 0
/* Zone file format, v1 (4-byte ints) only for now.
   See tzinfo(5) for a verbose description. Sadly, C does not allow
   variable structures, so this is only for reference. */

struct tzfile
{
	char magic[4];      /* "TZif" */
	char version[1];    /* '\0' or '2' */
	char _[15];

	int32_t tzh_ttisgmtcnt;
	int32_t tzh_ttisstdcnt;
	int32_t tzh_leapcnt;
	int32_t tzh_timecnt;
	int32_t tzh_typecnt;
	int32_t tzh_charcnt;

	int32_t tzh_times[tzh_timecnt];
	uint8_t tzh_ttype[tzh_timecnt];

	struct ttinfo {
		int32_t tt_gmtoff;
		uint8_t tt_isdst;
		uint8_t tt_abbrind;
	} tzh_types[tzh_typecnt];

	struct tzleap {
		int32_t base;
		int32_t leap;
	} tzh_leaps[tzh_leapcnt];

	uint8_t tzh_ttisstd[tzh_ttisstdcnt];
	uint8_t tzh_ttisgmt[tzh_ttisgmtcnt];
}
#endif

struct tzh {
	int ttisgmtcnt;
	int ttisstdcnt;
	int leapcnt;
	int timecnt;
	int typecnt;
	int charcnt;

	uint8_t* times;
	uint8_t* ttype;
	uint8_t* types;
	uint8_t* chars;
	uint8_t* leaps;
};

/* big endian 4-byte int at */
static int32_t beint32(uint8_t* p)
{
	return (p[0] << 3*8) | (p[1] << 2*8) | (p[2] << 1*8) | p[3];
};

static void tz_init(struct zonefile* zf, struct tzh* tzh)
{
	uint8_t* data = (uint8_t*) zf->data;
	const char* name = zf->name;
	int len = zf->len;

	if(len < 50 || strncmp(zf->data, "TZif", 4))
		fail("not a TZif:", name, 0);

	tzh->ttisgmtcnt = beint32(data + 20 + 0*4);
	tzh->ttisstdcnt = beint32(data + 20 + 1*4);
	tzh->leapcnt = beint32(data + 20 + 2*4);
	tzh->timecnt = beint32(data + 20 + 3*4);
	tzh->typecnt = beint32(data + 20 + 4*4);
	tzh->charcnt = beint32(data + 20 + 5*4);

	/* Bogus length? */
	if(tzh->timecnt < 0 || tzh->typecnt < 0)
		fail("bogus values in", name, 0);

	/* expected length, given *count values so far */
	int expfilelen = 20 + 6*4 		/* header and counters */
		+ tzh->timecnt*4		/* tzh_times */
		+ tzh->timecnt*1		/* tzh_ttype */
		+ (4 + 1 + 1)*tzh->typecnt;	/* tzh_types */
	if(expfilelen > len || expfilelen < 0)
		fail("bogus data length in", name, 0);

	tzh->times = data + 20 + 6*4;
	tzh->ttype = tzh->times + 4*tzh->timecnt;
	tzh->types = tzh->ttype + tzh->timecnt;
	tzh->chars = tzh->types + 6*tzh->typecnt;
	tzh->leaps = tzh->chars + tzh->charcnt;
}

static time_t tz_times(struct tzh* tz, int i)
{
	if(i < 0 || i >= tz->timecnt)
		return 0;
	return (time_t) beint32(tz->times + 4*i);
}

static int tz_ttype(struct tzh* tz, int i)
{
	if(i < 0 || i >= tz->timecnt)
		return 0;
	return tz->ttype[i];
}

static int tz_types(struct tzh* tz, int i)
{
	if(i < 0 || i >= tz->typecnt)
		return 0;
	return beint32(tz->types + 6*i);
}

static int tz_timecnt(struct tzh* tz)
{
	return tz->timecnt;
}

static int tz_leapcnt(struct tzh* tz)
{
	return tz->leapcnt;
}

static int tz_leapsum(struct tzh* tz, int i)
{
	return beint32(tz->leaps + 8*i + 4);
}

static int tz_leapbase(struct tzh* tz, int i)
{
	return beint32(tz->leaps + 8*i);
}

static void zone_shift_fwd(struct zonefile* zf, time_t ts, struct zoneshift* zx)
{
	struct tzh tz;
	struct tzh* tp = &tz;
	int i;

	tz_init(zf, tp);

	int lo = 0;
	int ls = 0;
	for(i = 0; i < tz_leapcnt(tp); i++)
		if(tz_leapbase(tp, i) < ts) {
			lo = tz_leapsum(tp, i);
		} else if(tz_leapbase(tp, i) == ts) {
			ls = tz_leapsum(tp, i) - lo;
			break;
		} else {
			break;
		}

	zx->leapoff = lo;
	zx->leapsec = ls;

	if(zf->fixed) {
		zx->zoneoff = zf->offset;
		return;
	}

	for(i = 0; i < tz_timecnt(tp); i++)
		if(tz_times(tp, i) >= ts)
			break;
	/* And btw, NOT breaking anywhere is ok for us as well, it means
	   there's no upper boundary for current time. */
	/* note i is the *next* interval index */
	int tt = tz_ttype(tp, i > 0 ? i - 1 : 0);
	int zo = tz_types(tp, tt);
	zx->zoneoff = zo;
}

/* Find zoneshift zx such that zone_shift_fwd(ts - zx) = zx.
   The following assumes that DST shifts never occur within about zoneoff
   of leap seconds. Which is typically true. */

static void zone_shift_rev(struct zonefile* zf, time_t ts, struct zoneshift* zx)
{
	struct tzh tz;
	struct tzh* tp = &tz;
	int i;

	tz_init(zf, tp);

	int leapoff = 0;
	int leapsec = 0;
	for(i = 0; i < tz_leapcnt(tp); i++) {
		int loff = tz_leapsum(tp, i);
		int lbase = tz_leapbase(tp, i);

		/* tsorig + loff == ts */
		/* tsorig == lbase && tsorig + lprev + lsec == ts */

		if(lbase < ts - loff - 1) {
			leapoff = loff;
		} else if(lbase == ts - loff - 1) {
			leapoff = loff;
			leapsec = 1;
			break;
		} else if(lbase == ts - loff) {
			leapoff = loff;
			break;
		} else {
			break;
		}
	}

	zx->leapoff = leapoff;
	zx->leapsec = leapsec;
	ts -= leapoff + leapsec;

	int zoneoff = 0;

	if(zf->fixed) {
		zoneoff = zf->offset;
	} else {
		for(i = 0; i < tz_timecnt(tp); i++) {
			time_t ttime = tz_times(tp, i);
			int ttype = tz_ttype(tp, i);
			int off = tz_types(tp, ttype);
			if(ttime < ts - off)
				zoneoff = off;
			else
				break;
		}
	}

	zx->zoneoff = zoneoff;
}

/*  What exactly is stored in zt depends on user input. It may be
   a unix timestamp (type=TIME_UNIX) or split datetime in tm (TIME_TM*),
   in which case we must reverse-map it into Unix time first. */

static void transback_tm_all_to_tv(struct timedesc* zt, struct zonefile* zf)
{
	struct zoneshift zx;

	tm2tv(&zt->tm, &zt->tv);

	zone_shift_rev(zf, zt->tv.tv_sec, &zx);

	zt->tv.tv_sec -= zx.leapoff + zx.leapsec + zx.zoneoff;

	zt->type = TIME_TV;
}

/* HMS only transback is tricky. 15:00 PDT (UTC-7) issued at
   2016-08-01 08:00 EEST (UTC+3) = 2016-07-31 22:00 PDT should
   probably be interpreted as 2016-07-31 15:00 PDT and not as
   2016-08-01 15:00 PDT, i.e. given time at current date
   in the *source* time zone. This means we must first translate
   system time (supplied to us in zt->tv) into PDT tm, substitute
   HMS part, and only then proceed with tm->tv translation. */

static void transback_tm_hms_to_tv(struct timedesc* zt, struct zonefile* zf)
{
	time_t ts = zt->tv.tv_sec; /* current system time */

	struct zoneshift zsh;
	zone_shift_fwd(zf, ts, &zsh);

	struct timeval tv = {
		.tv_sec = ts + zsh.leapoff + zsh.zoneoff,
		.tv_usec = 0
	};
	struct tm tm;

	int hour = zt->tm.tm_hour;
	int min = zt->tm.tm_min;
	int sec = zt->tm.tm_sec;

	tv2tm(&tv, &tm);
	memcpy(&zt->tm, &tm, sizeof(tm));
	zt->tm.tm_hour = hour;
	zt->tm.tm_min = min;
	zt->tm.tm_sec = sec;
	zt->type = TIME_TM_ALL;

	transback_tm_all_to_tv(zt, zf);
}

static void translate_tv_to_local_tm(struct timedesc* zt, struct zonefile* tgt)
{
	time_t ts = zt->tv.tv_sec;
	struct zoneshift zsh;

	zone_shift_fwd(tgt, ts, &zsh);

	struct timeval local = {
		.tv_sec = ts + zsh.zoneoff + zsh.leapoff,
		.tv_usec = zt->tv.tv_usec
	};

	tv2tm(&local, &zt->tm);

	zt->tm.tm_sec += zsh.leapsec;

	zt->type = TIME_LOCAL;
	zt->zone = NULL;
	zt->offset = zsh.zoneoff;
}

void translate(struct timedesc* zt, struct zonefile* src, struct zonefile* tgt)
{
	if(zt->type == TIME_TM_HMS)
		transback_tm_hms_to_tv(zt, src);
	else if(zt->type == TIME_TM_ALL)
		transback_tm_all_to_tv(zt, src);

	/* zt->tv is now a proper unix timestamp */

	translate_tv_to_local_tm(zt, tgt);
	
	/* zt->tm is now proper local time representation */
}
