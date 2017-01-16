#include <bits/fcntl.h>
#include <syscall.h>

inline static long syspipe2(int* fds, int flags)
{
	return syscall2(__NR_pipe2, (long)fds, flags);
}
