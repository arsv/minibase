#include <syscall.h>

inline static long sysbind(int fd, void* addr, int len)
{
	return syscall3(__NR_bind, fd, (long)addr, len);
}
