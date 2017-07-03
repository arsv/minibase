#include <syscall.h>

inline static long sys_umask(int mask)
{
	return syscall1(__NR_umask, mask);
}
