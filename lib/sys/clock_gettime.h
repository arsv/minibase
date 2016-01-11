#include <bits/syscall.h>
#include <syscall.h>

struct timespec;

inline static long sysclock_gettime(long clk_id, struct timespec* tp)
{
	return syscall2(__NR_clock_gettime, clk_id, (long)tp);
}
