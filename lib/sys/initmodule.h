#include <bits/syscall.h>
#include <syscall3.h>

inline static long sysinitmodule(void* image, unsigned long length,
		const char* params)
{
	return syscall3(__NR_init_module, (long)image, length, (long)params);
}
