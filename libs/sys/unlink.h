#include <bits/syscall.h>
#include <syscall1.h>

inline static long sysunlink(const char* name)
{
	return syscall1(__NR_unlink, (long)name);
}
