#include <syscall.h>

inline static long syssync(void)
{
	return syscall0(__NR_sync);
}
