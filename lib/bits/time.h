#ifndef __BITS_TIME_H__
#define __BITS_TIME_H__

#include <bits/types.h>

struct tm {
	int sec;
	int min;
	int hour;
	int mday;
	int mon;
	int year;
	int wday;
	int yday;
	int isdst;
};

/* Basic kernel struct, long will be of different length
   on 32 and 64 arches, time_t is more or less guaranteed
   to be 64-bit even on 32 bit systems. */

struct timeval {
	time_t sec;
	long usec;
};

/* Same as timeval, except with nanoseconds. */

struct timespec {
	time_t sec;
	long nsec;
};

/* Not used anywhere */

struct timezone {
	int minuteswest;
	int dsttime;
};

/* getitimer, setitimer */

struct itimerval {
	struct timeval interval;
	struct timeval value;
};

#endif
