#include <syscall.h>
#include <bits/fcntl.h>

inline static long syssymlink(const char *target, const char *linkpath)
{
	return syscall3(__NR_symlinkat, (long)target, AT_FDCWD, (long)linkpath);
}
