#include <syscall.h>

inline static long syspause(void)
{
	return syscall0(__NR_pause);
}
