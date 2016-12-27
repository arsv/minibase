#include <syscall.h>

inline static long sysconnect(int fd, void* addr, int len)
{
	return syscall3(__NR_connect, fd, (long)addr, len);
}
