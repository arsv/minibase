#include <syscall.h>
#include <bits/types.h>

inline static long sysftruncate(int fd, uint64_t size)
{
	return syscall2(__NR_ftruncate, fd, size);
}
