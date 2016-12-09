#include <syscall.h>

struct sockaddr;

inline static long sysbind(int fd, const struct sockaddr* addr, int len)
{
	return syscall3(__NR_bind, fd, (long)addr, len);
}
