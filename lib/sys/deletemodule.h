#include <syscall.h>
#include <bits/fcntl.h>

inline static long sysdeletemodule(const char* name, int flags)
{
	return syscall2(__NR_delete_module, (long)name, flags);
}
