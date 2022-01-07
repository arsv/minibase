#ifndef __BITS_TIME_H__
#define __BITS_TIME_H__

#include <bits/types.h>

/* Ref. linux/include/uapi/linux/time.h */

#define CLOCK_REALTIME              0
#define CLOCK_MONOTONIC             1
#define CLOCK_PROCESS_CPUTIME_ID    2
#define CLOCK_THREAD_CPUTIME_ID     3
#define CLOCK_MONOTONIC_RAW         4
#define CLOCK_REALTIME_COARSE       5
#define CLOCK_MONOTONIC_COARSE      6
#define CLOCK_BOOTTIME              7
#define CLOCK_REALTIME_ALARM        8
#define CLOCK_BOOTTIME_ALARM        9
#define CLOCK_SGI_CYCLE            10
#define CLOCK_TAI                  11

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

#endif
