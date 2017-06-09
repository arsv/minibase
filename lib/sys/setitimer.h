#include <bits/time.h>
#include <syscall.h>

#define	ITIMER_REAL     0
#define	ITIMER_VIRTUAL  1
#define	ITIMER_PROF     2

inline static long syssetitimer(int which, struct itimerval* itv, struct itimerval* old)
{
	return syscall3(__NR_setitimer, which, (long)itv, (long)old);
}
