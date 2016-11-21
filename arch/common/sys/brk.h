#include <syscall.h>

inline static long sysbrk(void* ptr)
{
	return syscall1(__NR_brk, (long)ptr);
}
