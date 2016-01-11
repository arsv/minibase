#include <bits/syscall.h>
#include <syscall.h>

struct statfs;

inline static long sysstatfs(const char* path, struct statfs* st)
{
	return syscall2(__NR_statfs, (long)path, (long)st);
}
