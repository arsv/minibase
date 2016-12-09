#include <syscall.h>

inline static long sysumask(int mode)
{
	return syscall1(__NR_umask, mode);
}
