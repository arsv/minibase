#include <syscall.h>

inline static long syssendto(int fd, const void* buf, int len, int flags,
		void* addr, int addrlen)
{
	return syscall6(__NR_sendto, fd, (long)buf, len, flags,
			(long)addr, addrlen);
}
