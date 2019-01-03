#include <syscall.h>

inline static long sys_getrandom(void* buf, size_t len, int flags)
{
	return syscall3(NR_getrandom, (long)buf, len, flags);
}
