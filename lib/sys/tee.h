#include <syscall.h>

inline static long systee(int fdin, int fdout, unsigned long len, unsigned flags)
{
	return syscall4(__NR_tee, fdin, fdout, len, flags);
}
