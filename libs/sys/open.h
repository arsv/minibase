#include <bits/syscall.h>
#include <bits/fcntl.h>
#include <syscall3.h>

inline static long sysopen(const char* name, int flags)
{
	return syscall3(__NR_openat, AT_FDCWD, (long)name, flags);
}
