#include <bits/types.h>
#include <string.h>
#include <fail.h>
#include "date.h"

#if 0
/* See tzinfo(5) for a verbose description. Sadly, C does not allow
   variable structures, so this is only for reference. */

struct tzfile
{
	char magic[4];		/* "TZif" */
	char version[1];	/* '\0' or '2' */
	char _[15];

	long tzh_ttisgmtcnt;
	long tzh_ttisstdcnt;
	long tzh_leapcnt;
	long tzh_timecnt;
	long tzh_typecnt;
	long tzh_charcnt;

	long tzh_times[tzh_timecnt];
	unsigned char tzh_ttype[tzh_timecnt];
	
	struct ttinfo {
		long tt_gmtoff;
		unsigned char tt_isdst;
		unsigned char tt_abbrind;
	} tzh_types[tzh_typecnt];

	struct tzleap {
		long base;
		long leap;
	} tzh_leaps[tzh_leapcnt];

	unsigned char tzh_ttisstd[tzh_ttisstdcnt];
	unsigned char tzh_ttisgmt[tzh_ttisgmtcnt];
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

void zone_shift_fwd(struct zonefile* zf, time_t ts, struct zoneshift* zx)
{
	struct tzh tz;
	struct tzh* tp = &tz;
	int i;

	tz_init(zf, &tz);
	
	int lo = 0;
	int ls = 0;
	for(i = 0; i < tz_leapcnt(tp); i++)
		if(tz_leapbase(tp, i) < ts) {
			lo = tz_leapsum(tp, i);
		} else if(tz_leapbase(tp, i) == ts) {
			ls = tz_leapsum(tp, i) - lo;
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

void zone_shift_rev(struct zonefile* zf, time_t ts, struct zoneshift* zx)
{
	zx->leapoff = 0;
	zx->leapsec = 0;
	zx->zoneoff = 0;
}
