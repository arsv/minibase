#include <bits/syscall.h>
#include <syscall.h>

inline static long sysgetpid(void)
{
	return syscall0(__NR_getpid);
}
