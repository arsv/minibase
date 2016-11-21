#include <syscall.h>
#include <bits/fcntl.h>

inline static long sysmknod(const char* pathname, int mode, int dev)
{
	return syscall4(__NR_mknodat, AT_FDCWD, (long)pathname, mode, dev);
}
