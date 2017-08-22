#include <syscall.h>

inline static long sys_getuid(void)
{
	return syscall0(__NR_getuid);
}

inline static long sys_getgid(void)
{
	return syscall0(__NR_getgid);
}

inline static long sys_geteuid(void)
{
	return syscall0(__NR_geteuid);
}

inline static long sys_getresuid(int* ruid, int* euid, int* suid)
{
	return syscall3(__NR_getresuid, (long)ruid, (long)euid, (long)suid);
}

inline static long sys_getresgid(int* rgid, int* egid, int* sgid)
{
	return syscall3(__NR_getresgid, (long)rgid, (long)egid, (long)sgid);
}

inline static long sys_setresuid(int ruid, int euid, int suid)
{
	return syscall3(__NR_setresuid, ruid, euid, suid);
}

inline static long sys_setresgid(int rgid, int egid, int sgid)
{
	return syscall3(__NR_setresgid, rgid, egid, sgid);
}

inline static long sys_getgroups(int size, int* list)
{
	return syscall2(__NR_getgroups, size, (long)list);
}

inline static long sys_setgroups(int size, const int* list)
{
	return syscall2(__NR_setgroups, size, (long)list);
}

inline static long sys_umask(int mode)
{
	return syscall1(__NR_umask, mode);
}
