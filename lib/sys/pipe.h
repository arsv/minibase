#include <syscall.h>
#include <bits/fcntl.h>

inline static long sys_pipe(int* fds)
{
#ifdef __NR_pipe2
	return syscall2(__NR_pipe2, (long)fds, 0);
#else
	return syscall1(__NR_pipe, (long)fds);
#endif
}

inline static long sys_pipe2(int* fds, int flags)
{
	return syscall2(__NR_pipe2, (long)fds, flags);
}
