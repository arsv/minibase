#include <syscall.h>

inline static long sys_getsid(void)
{
	return syscall0(__NR_getsid);
}

inline static long sys_setsid(void)
{
	return syscall0(__NR_setsid);
}

inline static long sys_getpgid(int pid)
{
	return syscall1(__NR_getpgid, pid);
}

inline static long sys_setpgid(int pid, int pgid)
{
	return syscall2(__NR_setpgid, pid, pgid);
}
