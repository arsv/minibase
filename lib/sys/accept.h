#include <syscall.h>

struct sockaddr;

inline static long sysaccept(int fd, struct sockaddr* addr, int* len)
{
	return syscall3(__NR_accept, fd, (long)addr, (long)len);
}
