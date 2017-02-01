#include <bits/types.h>
#include <syscall.h>

inline static long sysrecv(int fd, void* buf, size_t len, int flags)
{
	return syscall6(__NR_recvfrom, fd, (long)buf, len, flags, 0, 0);
}
