#include <syscall.h>
#include <bits/time.h>
#include <bits/sigevent.h>

#define ITIMER_REAL     0
#define ITIMER_VIRTUAL  1
#define ITIMER_PROF     2

struct itimerval {
	struct timeval interval;
	struct timeval value;
};

struct itimerspec {
	struct timespec interval;
	struct timespec value;
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

inline static int sys_timer_create(int which, struct sigevent* sevt, int* timerid)
{
	return syscall3(NR_timer_create, which, (long)sevt, (long)timerid);
}

inline static int sys_timer_delete(int timerid)
{
	return syscall1(NR_timer_delete, timerid);
}

inline static int sys_timer_settime(int timerid, int flags,
		const struct itimerspec* newtime, struct itimerspec* oldtime)
{
	return syscall4(NR_timer_settime, timerid, flags,
			(long)newtime, (long)oldtime);
}

inline static int sys_timer_gettime(int timerid, struct itimerspec* curtime)
{
	return syscall2(NR_timer_gettime, timerid, (long)curtime);
}
