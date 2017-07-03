#ifndef __BITS_RTC_H__
#define __BITS_RTC_H__

/* See also: <linux/rtc.h> */

struct rtc_time {
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

#endif
