#include <syscall.h>

inline static long sys_alarm(unsigned int seconds)
{
	return syscall1(__NR_alarm, seconds);
}
