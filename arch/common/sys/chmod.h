#include <syscall.h>
#include <bits/fcntl.h>

inline static long syschmod(const char* filename, int mode)
{
#ifdef __NR_fchmodat
	return syscall4(__NR_fchmodat, AT_FDCWD, (long)filename, mode, 0);
#else
	return syscall2(__NR_chmod, (long)filename, mode);
#endif
}
