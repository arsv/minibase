#include <syscall.h>

inline static long sys_setsid(void)
{
	return syscall0(__NR_setsid);
}
