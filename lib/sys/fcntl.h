#include <syscall.h>
#include <bits/fcntl.h>

inline static long sysfcntl(int fd, int cmd)
{
	return syscall2(__NR_fcntl, fd, cmd);
}

inline static long sysfcntl3(int fd, int cmd, int arg)
{
	return syscall3(__NR_fcntl, fd, cmd, arg);
}
