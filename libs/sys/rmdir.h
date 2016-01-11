#include <bits/syscall.h>
#include <bits/fcntl.h>
#include <syscall3.h>

inline static long sysrmdir(const char* dir)
{
	return syscall3(__NR_unlinkat, AT_FDCWD, (long)dir, AT_REMOVEDIR);
}
