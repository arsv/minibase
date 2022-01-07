#include <bits/time.h>
#include <syscall.h>

inline static long sys_clock_getres(int id, struct timespec* tp)
{
	return syscall2(NR_clock_getres, id, (long)tp);
}

inline static long sys_clock_gettime(int id, struct timespec* tp)
{
	return syscall2(NR_clock_gettime, id, (long)tp);
}

inline static long sys_clock_settime(int id, const struct timespec* tp)
{
	return syscall2(NR_clock_settime, id, (long)tp);
}


inline static long sys_gettimeofday(struct timeval* tv,
                                    struct timezone* tz)
{
	return syscall2(NR_gettimeofday, (long)tv, (long)tz);
}

inline static long sys_settimeofday(const struct timeval* tv,
                                    const struct timezone* tz)
{
	return syscall2(NR_settimeofday, (long)tv, (long)tz);
}
