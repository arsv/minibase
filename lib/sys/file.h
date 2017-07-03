#include <syscall.h>
#include <bits/fcntl.h>
#include <bits/stdio.h>

/* Common file ops. The vast majority of use cases only needs
   these, so they are moved to their own header. Otherwise
   open() would be in filepath.h and the rest in fcntl.h. */

inline static long sys_open(const char* name, int flags)
{
	return syscall3(__NR_openat, AT_FDCWD, (long)name, flags);
}

inline static long sys_open3(const char* name, int flags, int mode)
{
	return syscall4(__NR_openat, AT_FDCWD, (long)name, flags, mode);
}

inline static long sys_openat(int at, const char* path, int flags)
{
	return syscall3(__NR_openat, at, (long)path, flags);
}

inline static long sys_close(int fd)
{
	return syscall1(__NR_close, fd);
}

inline static long sys_read(int fd, char* buf, unsigned long len)
{
	return syscall3(__NR_read, fd, (long)buf, len);
}

inline static long sys_write(int fd, const char* buf, int len)
{
	return syscall3(__NR_write, fd, (long)buf, len);
}
