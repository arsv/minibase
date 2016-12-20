#include <syscall.h>

inline static long syssend(int fd, const char* buf, int len, int flags)
{
	return syscall6(__NR_sendto, fd, (long)buf, len, flags, 0, 0);
}
