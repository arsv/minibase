#include <bits/time.h>
#include <syscall.h>

inline static long sys_alarm(unsigned int seconds)
{
	struct /* itimerval */ {
		struct timeval interval;
		struct timeval value;
	} itv = { { 0, 0 }, { seconds, 0 } };

	return syscall3(__NR_setitimer, 0 /* ITIMER_REAL */, (long)&itv, 0);
}
