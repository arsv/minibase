#include <bits/time.h>
#include <syscall.h>

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

inline static long sys_clock_getres(int id, struct timespec* tp)
{
	return syscall2(__NR_clock_getres, id, (long)tp);
}

inline static long sys_clock_gettime(int id, struct timespec* tp)
{
	return syscall2(__NR_clock_gettime, id, (long)tp);
}

inline static long sys_clock_settime(int id, const struct timespec* tp)
{
	return syscall2(__NR_clock_settime, id, (long)tp);
}


inline static long sys_gettimeofday(struct timeval* tv,
                                    struct timezone* tz)
{
	return syscall2(__NR_gettimeofday, (long)tv, (long)tz);
}

inline static long sys_settimeofday(const struct timeval* tv,
                                    const struct timezone* tz)
{
	return syscall2(__NR_settimeofday, (long)tv, (long)tz);
}
