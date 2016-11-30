#include <syscall.h>
#include <bits/wait.h>

inline static long syswaitpid(int pid, int* status, int flags)
{
	return syscall4(__NR_wait4, pid, (long)status, flags, 0);
}
