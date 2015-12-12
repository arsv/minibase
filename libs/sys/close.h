#include <bits/syscall.h>
#include <syscall1.h>

inline static long sysclose(int fd)
{
	return syscall1(__NR_close, fd);
}
