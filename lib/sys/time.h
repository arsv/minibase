#include <syscall.h>
#include <bits/time.h>

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
