#include <bits/syscall.h>
#include <syscall2.h>

struct stat;

inline static long sysstat(const char *path, struct stat *st)
{
	return syscall2(__NR_stat, (long)path, (long)st);
}
