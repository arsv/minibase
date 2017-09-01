#include <syscall.h>
#include <bits/stat.h>
#include <bits/ints.h>
#include <bits/fcntl.h>
#include <bits/stdio.h>

/* Common file ops. */

inline static long sys_open(const char* name, int flags)
{
	return syscall3(NR_openat, AT_FDCWD, (long)name, flags);
}

inline static long sys_open3(const char* name, int flags, int mode)
{
	return syscall4(NR_openat, AT_FDCWD, (long)name, flags, mode);
}

inline static long sys_openat(int at, const char* path, int flags)
{
	return syscall3(NR_openat, at, (long)path, flags);
}

inline static long sys_close(int fd)
{
	return syscall1(NR_close, fd);
}

inline static long sys_read(int fd, char* buf, unsigned long len)
{
	return syscall3(NR_read, fd, (long)buf, len);
}

inline static long sys_write(int fd, const char* buf, int len)
{
	return syscall3(NR_write, fd, (long)buf, len);
}

inline static long sys_fcntl(int fd, int cmd)
{
	return syscall2(NR_fcntl, fd, cmd);
}

inline static long sys_fcntl3(int fd, int cmd, int arg)
{
	return syscall3(NR_fcntl, fd, cmd, arg);
}

inline static long sys_dup2(int fda, int fdb)
{
	return syscall3(NR_dup3, fda, fdb, 0);
}

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

inline static long sys_lseek(int fd, uint64_t off, int whence)
{
	return syscall3(NR_lseek, fd, off, whence);
}

#ifndef NR_fstatat
#define NR_fstatat NR_newfstatat
#endif

inline static long sys_stat(const char *path, struct stat *st)
{
	return syscall4(NR_fstatat, AT_FDCWD, (long)path, (long)st, 0);
}

inline static long sys_fstat(int fd, struct stat* st)
{
	return syscall2(NR_fstat, fd, (long)st);
}

inline static long sys_fstatat(int dirfd, const char *path,
                               struct stat* st, int flags)
{
	return syscall4(NR_fstatat, dirfd, (long)path, (long)st, flags);
}

inline static long sys_lstat(const char *path, struct stat *st)
{
	return syscall4(NR_fstatat, AT_FDCWD, (long)path, (long)st,
                                                     AT_SYMLINK_NOFOLLOW);
}

inline static long sys_pipe(int* fds)
{
#ifdef NR_pipe2
	return syscall2(NR_pipe2, (long)fds, 0);
#else
	return syscall1(NR_pipe, (long)fds);
#endif
}

inline static long sys_pipe2(int* fds, int flags)
{
	return syscall2(NR_pipe2, (long)fds, flags);
}
