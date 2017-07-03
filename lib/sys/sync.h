#include <syscall.h>

inline static long sys_sync(void)
{
	return syscall0(__NR_sync);
}

inline static long sys_fsync(int fd)
{
	return syscall1(__NR_fsync, fd);
}

inline static long sys_fdatasync(int fd)
{
	return syscall1(__NR_fdatasync, fd);
}

inline static long sys_syncfs(int fd)
{
	return syscall1(__NR_syncfs, fd);
}
