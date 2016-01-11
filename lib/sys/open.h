#include <syscall.h>
#include <bits/fcntl.h>

inline static long sysopen(const char* name, int flags)
{
	return syscall3(__NR_openat, AT_FDCWD, (long)name, flags);
}

inline static long sysopen3(const char* name, int flags, int mode)
{
	return syscall4(__NR_openat, AT_FDCWD, (long)name, flags, mode);
}
