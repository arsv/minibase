#include <bits/syscall.h>
#include <syscall3.h>

inline static long sysmknod(const char* pathname, int mode, int dev)
{
	return syscall3(__NR_mknod, (long)pathname, mode, dev);
}
