#include <bits/syscall.h>
#include <bits/fcntl.h>
#include <syscall.h>

inline static long sysunlink(const char* name)
{
	return syscall2(__NR_unlinkat, AT_FDCWD, (long)name);
}
