#include <bits/syscall.h>
#include <bits/fcntl.h>
#include <syscall.h>

inline static long sysmkdir(const char* pathname, int mode)
{
	return syscall3(__NR_mkdirat, AT_FDCWD, (long)pathname, mode);
}
