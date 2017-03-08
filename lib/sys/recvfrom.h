#include <bits/types.h>
#include <syscall.h>

inline static long sysrecvfrom(int fd, void* buf, size_t len, int flags,
		void* addr, int* alen)
{
	return syscall6(__NR_recvfrom, fd, (long)buf, len, flags,
			(long)addr, (long)alen);
}
