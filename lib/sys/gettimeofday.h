#include <bits/syscall.h>
#include <syscall.h>

struct timeval;
struct timezone;

inline static long sysgettimeofday(struct timeval* tv, struct timezone* tz)
{
	return syscall2(__NR_gettimeofday, (long)tv, (long)tz);
}
