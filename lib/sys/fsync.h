#include <syscall.h>

inline static long sysfsync(int fd)
{
	return syscall1(__NR_fsync, fd);
}
