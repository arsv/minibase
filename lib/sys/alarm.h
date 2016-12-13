#include <syscall.h>

inline static long sysalarm(unsigned int seconds)
{
	return syscall1(__NR_alarm, seconds);
}
