#include <bits/syscall.h>
#include <syscall2.h>

inline static long sysmkdir(const char* pathname, int mode)
{
	return syscall2(__NR_mkdir, (long)pathname, mode);
}
