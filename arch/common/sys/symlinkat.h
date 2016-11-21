#include <syscall.h>
#include <bits/fcntl.h>

inline static long syssymlinkat(const char *target,
		int dirfd, const char *linkpath)
{
	return syscall3(__NR_symlinkat, (long)target, dirfd, (long)linkpath);
}
