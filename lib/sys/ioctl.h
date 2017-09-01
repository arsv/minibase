#include <syscall.h>
#include <bits/ioctl.h>

inline static long sys_ioctl(int fd, unsigned long request, void* arg)
{
	return syscall3(NR_ioctl, fd, request, (long)arg);
}

/* same, with immediate/integer argument */

inline static long sys_ioctli(int fd, unsigned long request, long arg)
{
	return syscall3(NR_ioctl, fd, request, arg);
}
