#include <syscall.h>

inline static long sys_setresuid(int ruid, int euid, int suid)
{
	return syscall3(__NR_setresuid, ruid, euid, suid);
}
