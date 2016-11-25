#include <string.h>
#include <time.h>
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

   We assume that leap seconds are correctly accounted for, in the same
   way, in *any* zone file. See zone handling routines on this.

   What exactly is stored in zt depends on user input. It may be
   a unix timestamp (type=TIME_UNIX) or split datetime in tm (TIME_TM*),
   in which case we must reverse-map it into Unix time first. */

static void transback_tm_all_to_tv(struct timedesc* zt, struct zonefile* zf)
{
	struct zoneshift zx;

	tm2tv(&zt->tm, &zt->tv);

	zone_shift_rev(zf, zt->tv.tv_sec, &zx);

	zt->tv.tv_sec -= zx.leapoff - zx.leapsec - zx.zoneoff;

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

	tv2tm(&tv, &tm);
	tm.tm_hour = zt->tm.tm_hour;
	tm.tm_min = zt->tm.tm_min;
	tm.tm_sec = zt->tm.tm_sec;
	zt->type = TIME_TM_ALL;

	transback_tm_all_to_tv(zt, zf);
}

static void prepare_zones(struct zonefile* tgt, struct zonefile* src,
		const char* tgtzone, const char* srczone)
{
	memset(tgt, 0, sizeof(*tgt));
	memset(src, 0, sizeof(*src));

	if(tgtzone)
		maybe_utc_zone(tgt, tgtzone);
	if(srczone)
		maybe_utc_zone(src, srczone);

	if(srczone && !src->fixed)
		open_named_zone(src, srczone);
	if(tgtzone && !tgt->fixed)
		open_named_zone(tgt, tgtzone);

	if(!tgt->data && !src->data)
		open_default_zone(tgt);
	else if(!src->data)
		link_zone_data(src, tgt);
	else if(!tgt->data)
		link_zone_data(tgt, src);

	/* TODO: no need to map zonefile for time-only translation
	   between two fixed-offset zones, i.e. `date EEST 7:40 EDT`.
	   The "time-only" clearly depends on the format. */
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

void translate(struct timedesc* zt, const char* targetzone)
{
	struct zonefile tgt;
	struct zonefile src;

	prepare_zones(&tgt, &src, targetzone, zt->zone);

	if(zt->type == TIME_TM_HMS)
		transback_tm_hms_to_tv(zt, &src);
	else if(zt->type == TIME_TM_ALL)
		transback_tm_all_to_tv(zt, &src);

	/* zt->tv is now a proper unix timestamp */

	translate_tv_to_local_tm(zt, &tgt);
	
	/* zt->tm is now proper local time representation */
}
