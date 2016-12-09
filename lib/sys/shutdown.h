#include <syscall.h>

inline static long sysshutdown(int fd, int how)
{
	return syscall2(__NR_shutdown, fd, how);
}
