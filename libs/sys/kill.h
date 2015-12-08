#include <bits/syscall.h>
#include <syscall2.h>

inline static long syskill(int pid, int sig)
{
	return syscall2(__NR_kill, pid, sig);
}
