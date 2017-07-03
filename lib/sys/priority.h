#include <syscall.h>

inline static long sys_getpriority(int which, int who, int nice)
{
	return syscall3(__NR_getpriority, which, who, nice);
}

inline static long sys_setpriority(int which, int who, int nice)
{
	return syscall3(__NR_setpriority, which, who, nice);
}

