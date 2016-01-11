#include <bits/syscall.h>
#include <syscall.h>

struct timeval;
struct timezone;

inline static long syssettimeofday(const struct timeval* tv,
		const struct timezone* tz)
{
	return syscall2(__NR_settimeofday, (long)tv, (long)tz);
}
