#include <bits/syscall.h>
#include <syscall.h>

inline static long sysmmap(void* addr, unsigned long length, int prot,
		int flags, int fd, unsigned long offset)
{
	return syscall6(__NR_mmap, (long)addr, length, prot, flags, fd, offset);
}
