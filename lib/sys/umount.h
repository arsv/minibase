#include <syscall.h>
#include <bits/mount.h>

inline static long sysumount(const char* target, int flags)
{
	return syscall2(__NR_umount2, (long)target, flags);
}
