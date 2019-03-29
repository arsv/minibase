#include <bits/time.h>
#include <syscall.h>

inline static long sys_pause(void)
{
	return syscall4(NR_ppoll, 0, 0, 0, 0);
}

inline static long sys_nanosleep(struct timespec* req, struct timespec* rem)
{
	return syscall2(NR_nanosleep, (long)req, (long)rem);
}

inline static long sys_getpriority(int which, int who, int nice)
{
	return syscall3(NR_getpriority, which, who, nice);
}

inline static long sys_setpriority(int which, int who, int nice)
{
	return syscall3(NR_setpriority, which, who, nice);
}

#if BITS == 32
struct cpuset { ulong bits[32]; };
#else
struct cpuset { ulong bits[16]; };
#endif

inline static void cpuset_set(struct cpuset* mask, uint n)
{
	if(8*n > sizeof(mask->bits))
		return;

	int wordsize = 8*sizeof(mask->bits[0]);
	int wordidx = n / wordsize;
	int wordbit = n % wordsize;

	mask->bits[wordidx] |= (1UL << wordbit);
}

inline static int cpuset_get(struct cpuset* mask, uint n)
{
	if(8*n > sizeof(mask->bits))
		return 0;

	int wordsize = 8*sizeof(mask->bits[0]);
	int wordidx = n / wordsize;
	int wordbit = n % wordsize;

	return !!(mask->bits[wordidx] & (1UL << wordbit));
}

inline static long sys_sched_getaffinity(int pid, struct cpuset* mask)
{
	return syscall3(NR_sched_getaffinity, pid, sizeof(*mask), (long)mask);
}

inline static long sys_sched_setaffinity(int pid, struct cpuset* mask)
{
	return syscall3(NR_sched_setaffinity, pid, sizeof(*mask), (long)mask);
}
