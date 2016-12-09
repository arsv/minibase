#include <syscall.h>
#include <bits/signal.h>

inline static long sysfork(void)
{
#ifdef __NR_fork
	return syscall0(__NR_fork);
#else
	return syscall5(__NR_clone, SIGCHLD, 0, 0, 0, 0);
#endif
}
