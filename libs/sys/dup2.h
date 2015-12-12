#include <bits/syscall.h>
#include <syscall2.h>

inline static long sysdup2(int fda, int fdb)
{
	return syscall2(__NR_dup2, fda, fdb);
}
