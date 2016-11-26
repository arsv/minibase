#include <bits/types.h>
#include <bits/time.h>

#define TIME_TV 0
#define TIME_TM_ALL 1
#define TIME_TM_HMS 2
#define TIME_LOCAL 3

struct timedesc {
	struct timeval tv;
	struct tm tm;
	int type;
	char* zone;
	int offset;
};

struct zonefile {
	const char* name; /* zone file, for reporting */
	char* data;       /* mmaped */
	size_t len;
	int fixed;        /* if true, ignore zone file UTC offset */
	int offset;       /* and use this one instead */
};

struct zoneshift {
	int zoneoff;     /* seconds from UTC */
	int leapoff;     /* seconds from Unix to UTC */
	int leapsec;     /* this second is a leap one */
};

void translate(struct timedesc* zt, const char* zone);

void maybe_utc_zone(struct zonefile* zf, const char* zone);
void open_named_zone(struct zonefile* zf, const char* zone);
void open_default_zone(struct zonefile* zf);

void zone_shift_fwd(struct zonefile* zf, time_t ts, struct zoneshift* zx);
void zone_shift_rev(struct zonefile* zf, time_t ts, struct zoneshift* zx);
