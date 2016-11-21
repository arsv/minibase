#include <syscall.h>
#include <bits/time.h>

inline static long sysgettimeofday(struct timeval* tv, struct timezone* tz)
{
	return syscall2(__NR_gettimeofday, (long)tv, (long)tz);
}
