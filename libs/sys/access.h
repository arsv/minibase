#include <bits/syscall.h>
#include <bits/fcntl.h>
#include <syscall4.h>

inline static long sysaccess(const char* path, int mode)
{
	return syscall4(__NR_faccessat, AT_FDCWD, (long)path, mode, 0);
}
