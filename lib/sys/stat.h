#include <bits/syscall.h>
#include <bits/fcntl.h>
#include <syscall.h>

struct stat;

#ifndef __NR_fstatat
#define __NR_fstatat __NR_newfstatat
#endif

inline static long sysstat(const char *path, struct stat *st)
{
	return syscall4(__NR_fstatat, AT_FDCWD, (long)path, (long)st, 0);
}
