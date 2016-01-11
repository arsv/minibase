#include <syscall.h>
#include <bits/statfs.h>

inline static long sysstatfs(const char* path, struct statfs* st)
{
	return syscall2(__NR_statfs, (long)path, (long)st);
}
