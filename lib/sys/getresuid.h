#include <syscall.h>

inline static long sys_getresuid(int* ruid, int* euid, int* suid)
{
	return syscall3(__NR_getresuid, (long)ruid, (long)euid, (long)suid);
}
