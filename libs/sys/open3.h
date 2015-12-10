#include <bits/syscall.h>
#include <syscall3.h>

inline static long sysopen3(const char* name, int flags, int mode)
{
	return syscall3(__NR_open, (long)name, flags, mode);
}
