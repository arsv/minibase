#include <syscall.h>

inline static long sysaccept(int fd, void* addr, int* len)
{
	return syscall3(__NR_accept, fd, (long)addr, (long)len);
}
