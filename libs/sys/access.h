#include <bits/syscall.h>
#include <syscall2.h>

inline static long sysaccess(const char* path, int mode)
{
	return syscall2(__NR_access, (long)path, mode);
}
