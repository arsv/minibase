#include <syscall.h>
#include <bits/fcntl.h>

inline static long sysunlink(const char* name)
{
	return syscall3(__NR_unlinkat, AT_FDCWD, (long)name, 0);
}
