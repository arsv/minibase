#include <syscall.h>
#include <bits/types.h>
#include <bits/fcntl.h>

#define MODULE_INIT_IGNORE_MODVERSIONS  1
#define MODULE_INIT_IGNORE_VERMAGIC     2

inline static long sys_init_module(void* buf, size_t len, const char* params)
{
	return syscall3(__NR_init_module, (long)buf, len, (long)params);
}

inline static long sys_finit_module(int fd, const char* params, int flags)
{
	return syscall3(__NR_finit_module, fd, (long)params, flags);
}

inline static long sys_delete_module(const char* name, int flags)
{
	return syscall2(__NR_delete_module, (long)name, flags);
}
