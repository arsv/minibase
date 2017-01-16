#include <syscall.h>

inline static long syspipe(int* fds)
{
#ifdef __NR_pipe2
	return syscall2(__NR_pipe2, (long)fds, 0);
#else
	return syscall1(__NR_pipe, (long)fds);
#endif
}
