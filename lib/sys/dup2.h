#include <bits/syscall.h>
#include <syscall.h>

inline static long sysdup2(int fda, int fdb)
{
	return syscall3(__NR_dup3, fda, fdb, 0);
}
