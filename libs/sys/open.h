#include <bits/syscall.h>
#include <syscall2.h>

inline static long sysopen(const char* name, int flags)
{
	return syscall2(__NR_open, (long)name, flags);
}
