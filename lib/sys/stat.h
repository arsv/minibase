#include <syscall.h>
#include <bits/fcntl.h>
#include <bits/stat.h>

#ifndef __NR_fstatat
#define __NR_fstatat __NR_newfstatat
#endif

inline static long sys_stat(const char *path, struct stat *st)
{
	return syscall4(__NR_fstatat, AT_FDCWD, (long)path, (long)st, 0);
}

inline static long sys_fstat(int fd, struct stat* st)
{
	return syscall2(__NR_fstat, fd, (long)st);
}

inline static long sys_fstatat(int dirfd, const char *path,
                               struct stat* st, int flags)
{
	return syscall4(__NR_fstatat, dirfd, (long)path, (long)st, flags);
}

inline static long sys_lstat(const char *path, struct stat *st)
{
	return syscall4(__NR_fstatat, AT_FDCWD, (long)path, (long)st,
                                                     AT_SYMLINK_NOFOLLOW);
}
