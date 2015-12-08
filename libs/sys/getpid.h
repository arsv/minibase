#include <bits/syscall.h>
#include <syscall0.h>

inline static long sysgetpid(void)
{
	return syscall0(__NR_getpid);
}
