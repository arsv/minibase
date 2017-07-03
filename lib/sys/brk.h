#include <syscall.h>

inline static long sys_brk(void* ptr)
{
	return syscall1(__NR_brk, (long)ptr);
}
