#include <syscall.h>

inline static long sys_getpid(void)
{
	return syscall0(__NR_getpid);
}

inline static long sys_getppid(void)
{
	return syscall0(__NR_getppid);
}
