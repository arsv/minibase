#include <syscall.h>

inline static long sysgeteuid(void)
{
	return syscall0(__NR_geteuid);
}
