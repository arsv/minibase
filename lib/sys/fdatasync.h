#include <syscall.h>

inline static long sysfdatasync(int fd)
{
	return syscall1(__NR_fdatasync, fd);
}

