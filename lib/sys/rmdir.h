#include <bits/syscall.h>
#include <bits/fcntl.h>
#include <syscall.h>

inline static long sysrmdir(const char* dir)
{
	return syscall3(__NR_unlinkat, AT_FDCWD, (long)dir, AT_REMOVEDIR);
}
