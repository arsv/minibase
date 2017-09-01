#include <bits/signal.h>
#include <bits/time.h>
#include <syscall.h>

#define	ITIMER_REAL     0
#define	ITIMER_VIRTUAL  1
#define	ITIMER_PROF     2

struct itimerval {
	struct timeval interval;
	struct timeval value;
};

inline static long sys_getitimer(int which, struct itimerval* itv)
{
	return syscall2(NR_setitimer, which, (long)itv);
}

inline static long sys_setitimer(int which, struct itimerval* itv, struct itimerval* old)
{
	return syscall3(NR_setitimer, which, (long)itv, (long)old);
}

inline static long sys_alarm(unsigned int seconds)
{
	struct itimerval itv = { { 0, 0 }, { seconds, 0 } };

	return syscall3(NR_setitimer, ITIMER_REAL, (long)&itv, 0);
}

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
