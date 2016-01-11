#include <bits/syscall.h>
#include <bits/fcntl.h>
#include <syscall4.h>

inline static long sysopen3(const char* name, int flags, int mode)
{
	return syscall4(__NR_openat, AT_FDCWD, (long)name, flags, mode);
}
