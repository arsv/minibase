#include <syscall.h>
#include <bits/iovec.h>

static inline long sys_readv(int fd, struct iovec* iov, int cnt)
{
	return syscall3(NR_readv, fd, (long)iov, cnt);
}

static inline long sys_writev(int fd, struct iovec* iov, int cnt)
{
	return syscall3(NR_writev, fd, (long)iov, cnt);
}
