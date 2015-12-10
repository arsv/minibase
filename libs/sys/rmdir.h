#include <bits/syscall.h>
#include <syscall1.h>

inline static long sysrmdir(const char* dir)
{
	return syscall1(__NR_rmdir, (long)dir);
}
