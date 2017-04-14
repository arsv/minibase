#include <syscall.h>

inline static long sys_setpriority(int which, int who, int nice)
{
	return syscall3(__NR_setpriority, which, who, nice);
}
