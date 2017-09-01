#include <syscall.h>

inline static long sys_getpid(void)
{
	return syscall0(NR_getpid);
}

inline static long sys_getppid(void)
{
	return syscall0(NR_getppid);
}

inline static long sys_getuid(void)
{
	return syscall0(NR_getuid);
}

inline static long sys_getgid(void)
{
	return syscall0(NR_getgid);
}

inline static long sys_geteuid(void)
{
	return syscall0(NR_geteuid);
}

inline static long sys_getresuid(int* ruid, int* euid, int* suid)
{
	return syscall3(NR_getresuid, (long)ruid, (long)euid, (long)suid);
}

inline static long sys_getresgid(int* rgid, int* egid, int* sgid)
{
	return syscall3(NR_getresgid, (long)rgid, (long)egid, (long)sgid);
}

inline static long sys_setresuid(int ruid, int euid, int suid)
{
	return syscall3(NR_setresuid, ruid, euid, suid);
}

inline static long sys_setresgid(int rgid, int egid, int sgid)
{
	return syscall3(NR_setresgid, rgid, egid, sgid);
}

inline static long sys_getgroups(int size, int* list)
{
	return syscall2(NR_getgroups, size, (long)list);
}

inline static long sys_setgroups(int size, const int* list)
{
	return syscall2(NR_setgroups, size, (long)list);
}

inline static long sys_umask(int mode)
{
	return syscall1(NR_umask, mode);
}

inline static long sys_getsid(void)
{
	return syscall0(NR_getsid);
}

inline static long sys_setsid(void)
{
	return syscall0(NR_setsid);
}

inline static long sys_getpgid(int pid)
{
	return syscall1(NR_getpgid, pid);
}

inline static long sys_setpgid(int pid, int pgid)
{
	return syscall2(NR_setpgid, pid, pgid);
}
