#include <syscall.h>

struct sockaddr;

inline static long sysconnect(int fd, const struct sockaddr* addr, int len)
{
	return syscall3(__NR_connect, fd, (long)addr, len);
}
