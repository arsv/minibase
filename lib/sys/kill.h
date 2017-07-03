#include <syscall.h>
#include <bits/signal.h>

inline static long sys_kill(int pid, int sig)
{
	return syscall2(__NR_kill, pid, sig);
}
