#include <syscall.h>
#include <bits/types.h>

inline static long syslseek(int fd, uint64_t off, int whence)
{
	return syscall3(__NR_lseek, fd, off, whence);
}
