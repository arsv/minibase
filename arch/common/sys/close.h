#include <syscall.h>

inline static long sysclose(int fd)
{
	return syscall1(__NR_close, fd);
}
