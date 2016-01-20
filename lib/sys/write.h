#include <syscall.h>
#include <bits/stdio.h>

inline static long syswrite(int fd, const char* buf, int len)
{
	return syscall3(__NR_write, fd, (long)buf, len);
}
