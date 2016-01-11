#include <syscall.h>
#include <bits/fcntl.h>

inline static long sysunlinkat(int atfd, const char* name, int flags)
{
	return syscall3(__NR_unlinkat, atfd, (long)name, flags);
}
