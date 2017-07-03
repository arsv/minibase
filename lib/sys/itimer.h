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
	return syscall2(__NR_setitimer, which, (long)itv);
}

inline static long sys_setitimer(int which, struct itimerval* itv, struct itimerval* old)
{
	return syscall3(__NR_setitimer, which, (long)itv, (long)old);
}
