#include <syscall.h>
#include <bits/access.h>
#include <bits/fcntl.h>

inline static long sysaccess(const char* path, int mode)
{
	return syscall4(__NR_faccessat, AT_FDCWD, (long)path, mode, 0);
}
