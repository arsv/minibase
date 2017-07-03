#include <syscall.h>
#include <bits/signal.h>

inline static long sys_fork(void)
{
	return syscall5(__NR_clone, SIGCHLD, 0, 0, 0, 0);
}
