#ifndef __BITS_TIME_H__
#define __BITS_TIME_H__

#include <bits/types.h>

struct tm
{
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

/* Basic kernel struct, long will be of different length
   on 32 and 64 arches, time_t is more or less guaranteed
   to be 64-bit even on 32 bit systems. */

struct timeval
{
	time_t tv_sec;		/* Seconds. */
	long  tv_usec;		/* Microseconds. */
};

#endif
