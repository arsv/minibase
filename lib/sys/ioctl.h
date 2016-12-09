#include <syscall.h>
#include <bits/ioctl.h>

inline static long sysioctl(int fd, unsigned long request, long arg)
{
	return syscall3(__NR_ioctl, fd, request, arg);
}
