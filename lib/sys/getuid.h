#include <syscall.h>

inline static long sysgetuid(void)
{
	return syscall0(__NR_getuid);
}
