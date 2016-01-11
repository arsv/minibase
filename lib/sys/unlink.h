#include <syscall.h>
#include <bits/fcntl.h>

inline static long sysunlink(const char* name)
{
	return syscall2(__NR_unlinkat, AT_FDCWD, (long)name);
}
