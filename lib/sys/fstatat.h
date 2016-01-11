#include <bits/syscall.h>
#include <syscall.h>

struct stat;

#ifndef __NR_fstatat
#define __NR_fstatat __NR_newfstatat
#endif

inline static long sysfstatat(int dirfd, const char *path,
		struct stat *buf, int flags)
{
	return syscall4(__NR_fstatat, dirfd, (long)path, (long)buf, flags);
}
