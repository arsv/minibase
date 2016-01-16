#include <syscall.h>

inline static long sysmunmap(void* ptr, unsigned long len)
{
	return syscall2(__NR_munmap, (long)ptr, len);
}
