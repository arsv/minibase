#include <syscall.h>
#include <bits/time.h>

inline static long syssettimeofday(const struct timeval* tv,
		const struct timezone* tz)
{
	return syscall2(__NR_settimeofday, (long)tv, (long)tz);
}
